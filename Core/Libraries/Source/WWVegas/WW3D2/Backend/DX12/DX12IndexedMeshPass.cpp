/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License, version 3 or later.
*/

#include "DX12IndexedMeshPass.h"
#include "DX12PresentationDevice.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr UINT MaxTextures = 128;
    constexpr DXGI_FORMAT TextureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT TargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    struct DrawConstants
    {
        float wvp[16];
        float factor[4];
        UINT alphaTest;
        UINT alphaReference;
        UINT alphaCompare;
        UINT unused;
    };
    static_assert(sizeof(DrawConstants) == 24 * sizeof(UINT), "Root constants must match W3DPerDraw");
    static_assert(sizeof(DX12W3DVertex) == 11 * sizeof(float), "Unexpected W3D unlit vertex layout");

    D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES properties = {};
        properties.Type = type;
        properties.CreationNodeMask = 1;
        properties.VisibleNodeMask = 1;
        return properties;
    }

    D3D12_RESOURCE_DESC Buffer(UINT64 byteCount)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = byteCount;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return desc;
    }

    D3D12_RESOURCE_DESC Texture(unsigned width, unsigned height)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = TextureFormat;
        desc.SampleDesc.Count = 1;
        return desc;
    }

    HRESULT CreateResource(ID3D12Device * device, D3D12_HEAP_TYPE heapType,
                           const D3D12_RESOURCE_DESC & description,
                           D3D12_RESOURCE_STATES state, ComPtr<ID3D12Resource> & output)
    {
        const auto properties = Heap(heapType);
        return device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
                &description, state, nullptr, IID_PPV_ARGS(&output));
    }

    D3D12_RESOURCE_BARRIER Barrier(ID3D12Resource * resource,
                                   D3D12_RESOURCE_STATES before,
                                   D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER transition = {};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition.pResource = resource;
        transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        transition.Transition.StateBefore = before;
        transition.Transition.StateAfter = after;
        return transition;
    }

    bool Failure(std::string & error, const char * label, HRESULT result)
    {
        char code[16] = {};
        std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(result));
        error = std::string(label) + ": " + code;
        return false;
    }

    bool Check(std::string & error, HRESULT result, const char * label)
    {
        return SUCCEEDED(result) || Failure(error, label, result);
    }

    bool CopyToUpload(ID3D12Resource * resource, const void * source, SIZE_T bytes,
                      std::string & error)
    {
        void * mapped = nullptr;
        if (!Check(error, resource->Map(0, nullptr, &mapped), "Map upload resource"))
            return false;
        std::memcpy(mapped, source, bytes);
        resource->Unmap(0, nullptr);
        return true;
    }

    ComPtr<ID3DBlob> CompileShader(const wchar_t * file, const char * entry,
                                  const char * model, const D3D_SHADER_MACRO * defines,
                                  std::string & error)
    {
        ComPtr<ID3DBlob> bytecode, compilerErrors;
        const HRESULT result = D3DCompileFromFile(
            file, defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, model,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &compilerErrors);
        if (FAILED(result))
        {
            Failure(error, "D3DCompileFromFile", result);
            if (compilerErrors)
                error += ": " + std::string(
                    static_cast<const char *>(compilerErrors->GetBufferPointer()),
                    compilerErrors->GetBufferSize());
        }
        return SUCCEEDED(result) ? bytecode : nullptr;
    }

    D3D12_STATIC_SAMPLER_DESC PointClampSampler(UINT shaderRegister)
    {
        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        sampler.MinLOD = 0;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = shaderRegister;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        return sampler;
    }
}

struct DX12W3DIndexedMesh::Impl
{
    ComPtr<ID3D12Resource> vertices;
    ComPtr<ID3D12Resource> indices;
    // Upload buffers are retained until Reset; callers are responsible for
    // GPU synchronization before Reset/destruction.
    ComPtr<ID3D12Resource> vertexUpload;
    ComPtr<ID3D12Resource> indexUpload;
    D3D12_VERTEX_BUFFER_VIEW vertexView = {};
    D3D12_INDEX_BUFFER_VIEW indexView = {};
    unsigned indexCount = 0;
    std::string error;
};

DX12W3DIndexedMesh::DX12W3DIndexedMesh() : impl_(new Impl) {}
DX12W3DIndexedMesh::~DX12W3DIndexedMesh() { Reset(); }

void DX12W3DIndexedMesh::Reset()
{
    impl_->vertices.Reset();
    impl_->indices.Reset();
    impl_->vertexUpload.Reset();
    impl_->indexUpload.Reset();
    impl_->vertexView = {};
    impl_->indexView = {};
    impl_->indexCount = 0;
}

void DX12W3DIndexedMesh::Release_Upload_Buffers()
{
    impl_->vertexUpload.Reset();
    impl_->indexUpload.Reset();
}

bool DX12W3DIndexedMesh::IsReady() const
{
    return impl_->vertices && impl_->indices && impl_->indexCount > 0;
}
unsigned DX12W3DIndexedMesh::IndexCount() const { return impl_->indexCount; }
const char * DX12W3DIndexedMesh::LastError() const { return impl_->error.c_str(); }

bool DX12W3DIndexedMesh::Upload(DX12PresentationDevice & presentation,
                               const DX12W3DVertex * vertices, unsigned vertexCount,
                               const std::uint16_t * indices, unsigned indexCount)
{
    Impl & data = *impl_;
    data.error.clear();
    if (IsReady() || !presentation.ActiveCommandList() || !presentation.NativeDevice() ||
        !vertices || !indices || vertexCount == 0 || vertexCount > 65536 ||
        indexCount == 0 || indexCount % 3 != 0 ||
        vertexCount > std::numeric_limits<UINT>::max() / sizeof(DX12W3DVertex) ||
        indexCount > std::numeric_limits<UINT>::max() / sizeof(std::uint16_t))
    {
        data.error = "Mesh upload requires an active frame and valid 16-bit triangle-list geometry";
        return false;
    }
    for (unsigned i = 0; i < indexCount; ++i)
    {
        if (indices[i] >= vertexCount)
        {
            data.error = "Mesh index out of vertex range";
            return false;
        }
    }

    const UINT vertexBytes = vertexCount * static_cast<UINT>(sizeof(DX12W3DVertex));
    const UINT indexBytes = indexCount * static_cast<UINT>(sizeof(std::uint16_t));
    ID3D12Device * device = presentation.NativeDevice();
    ID3D12GraphicsCommandList * commands = presentation.ActiveCommandList();
    if (!Check(data.error, CreateResource(device, D3D12_HEAP_TYPE_DEFAULT, Buffer(vertexBytes),
                      D3D12_RESOURCE_STATE_COPY_DEST, data.vertices), "Create GPU vertex buffer") ||
        !Check(data.error, CreateResource(device, D3D12_HEAP_TYPE_DEFAULT, Buffer(indexBytes),
                      D3D12_RESOURCE_STATE_COPY_DEST, data.indices), "Create GPU index buffer") ||
        !Check(data.error, CreateResource(device, D3D12_HEAP_TYPE_UPLOAD, Buffer(vertexBytes),
                      D3D12_RESOURCE_STATE_GENERIC_READ, data.vertexUpload), "Create vertex staging buffer") ||
        !Check(data.error, CreateResource(device, D3D12_HEAP_TYPE_UPLOAD, Buffer(indexBytes),
                      D3D12_RESOURCE_STATE_GENERIC_READ, data.indexUpload), "Create index staging buffer") ||
        !CopyToUpload(data.vertexUpload.Get(), vertices, vertexBytes, data.error) ||
        !CopyToUpload(data.indexUpload.Get(), indices, indexBytes, data.error))
    {
        Reset();
        return false;
    }

    commands->CopyBufferRegion(data.vertices.Get(), 0, data.vertexUpload.Get(), 0, vertexBytes);
    commands->CopyBufferRegion(data.indices.Get(), 0, data.indexUpload.Get(), 0, indexBytes);
    const std::array<D3D12_RESOURCE_BARRIER, 2> barriers = {{
        Barrier(data.vertices.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        Barrier(data.indices.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_INDEX_BUFFER)
    }};
    commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

    data.vertexView.BufferLocation = data.vertices->GetGPUVirtualAddress();
    data.vertexView.SizeInBytes = vertexBytes;
    data.vertexView.StrideInBytes = sizeof(DX12W3DVertex);
    data.indexView.BufferLocation = data.indices->GetGPUVirtualAddress();
    data.indexView.SizeInBytes = indexBytes;
    data.indexView.Format = DXGI_FORMAT_R16_UINT;
    data.indexCount = indexCount;
    return true;
}

struct DX12W3DIndexedMeshPass::Impl
{
    struct TextureRecord
    {
        ComPtr<ID3D12Resource> gpu;
        ComPtr<ID3D12Resource> upload;
    };

    DX12PresentationDevice * presentation = nullptr;
    std::wstring shaderPath;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3D12DescriptorHeap> texturesHeap;
    UINT descriptorStride = 0;
    std::vector<TextureRecord> textures;
    // Material combiner key alone cannot identify the blend/depth-write PSO.
    std::unordered_map<std::uint64_t, ComPtr<ID3D12PipelineState>> pipelines;
    std::string error;

    bool CheckResult(HRESULT hr, const char * label)
    {
        return Check(error, hr, label);
    }

    ID3D12Device * Device() const
    {
        return presentation ? presentation->NativeDevice() : nullptr;
    }

    ID3D12PipelineState * GetPipeline(UINT key, bool alphaBlend)
    {
        const std::uint64_t pipelineKey =
            (static_cast<std::uint64_t>(key) << 1) | (alphaBlend ? 1u : 0u);
        const auto found = pipelines.find(pipelineKey);
        if (found != pipelines.end())
            return found->second.Get();

        char keyMacro[24] = {};
        std::snprintf(keyMacro, sizeof(keyMacro), "0x%08Xu", key);
        const D3D_SHADER_MACRO macros[] = {
            {"W3D_STATIC_COMBINER_KEY", keyMacro},
            {nullptr, nullptr}
        };
        auto pixelShader = CompileShader(shaderPath.c_str(), "W3D_PS", "ps_5_1", macros, error);
        if (!pixelShader)
            return nullptr;

        const D3D12_INPUT_ELEMENT_DESC inputs[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
             static_cast<UINT>(offsetof(DX12W3DVertex, position)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
             static_cast<UINT>(offsetof(DX12W3DVertex, diffuse)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
             static_cast<UINT>(offsetof(DX12W3DVertex, uv0)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0,
             static_cast<UINT>(offsetof(DX12W3DVertex, uv1)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root.Get();
        desc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        desc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
        desc.InputLayout = {inputs, static_cast<UINT>(sizeof(inputs) / sizeof(inputs[0]))};
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        auto & target = desc.BlendState.RenderTarget[0];
        target.SrcBlend = D3D12_BLEND_ONE;
        target.DestBlend = D3D12_BLEND_ZERO;
        target.BlendOp = D3D12_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_ZERO;
        target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        target.LogicOp = D3D12_LOGIC_OP_NOOP;
        target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        // Opaque W3D geometry is depth tested irrespective of submission order.
        desc.DepthStencilState.DepthEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        if (alphaBlend)
        {
            target.BlendEnable = TRUE;
            target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            target.SrcBlendAlpha = D3D12_BLEND_ONE;
            target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            // Water can hide terrain in color but must not occlude later draws.
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        }
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = TargetFormat;
        desc.SampleDesc.Count = 1;

        ComPtr<ID3D12PipelineState> pipeline;
        if (!CheckResult(Device()->CreateGraphicsPipelineState(
                             &desc, IID_PPV_ARGS(&pipeline)), "Create indexed mesh PSO"))
            return nullptr;
        return pipelines.emplace(pipelineKey, std::move(pipeline)).first->second.Get();
    }
};

DX12W3DIndexedMeshPass::DX12W3DIndexedMeshPass() : impl_(new Impl) {}
DX12W3DIndexedMeshPass::~DX12W3DIndexedMeshPass() { Shutdown(); }

void DX12W3DIndexedMeshPass::Shutdown()
{
    Impl & state = *impl_;
    // The render pass owns descriptors and texture uploads. Keep them valid
    // until all GPU submissions that reference them have completed.
    if (state.presentation && state.presentation->IsReady())
        state.presentation->WaitForIdle();
    state.pipelines.clear();
    state.textures.clear();
    state.texturesHeap.Reset();
    state.vertexShader.Reset();
    state.root.Reset();
    state.shaderPath.clear();
    state.descriptorStride = 0;
    state.presentation = nullptr;
}

bool DX12W3DIndexedMeshPass::Initialize(DX12PresentationDevice & presentation,
                                        const wchar_t * shaderSource)
{
    Shutdown();
    Impl & state = *impl_;
    state.error.clear();
    if (!presentation.IsReady() || !shaderSource || !*shaderSource)
    {
        state.error = "Mesh renderer requires a ready DX12 device and W3D shader source";
        return false;
    }

    const D3D_SHADER_MACRO macros[] = {
        {"W3D_STATIC_COMBINER_KEY", "0x10101010u"},
        {nullptr, nullptr}
    };
    state.vertexShader = CompileShader(shaderSource, "W3D_VS", "vs_5_1", macros, state.error);
    if (!state.vertexShader)
        return false;

    ID3D12Device * device = presentation.NativeDevice();
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = sizeof(DrawConstants) / sizeof(UINT);
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (UINT i = 0; i < 2; ++i)
    {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = i;
        params[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i + 1].DescriptorTable.NumDescriptorRanges = 1;
        params[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
        params[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    const D3D12_STATIC_SAMPLER_DESC samplers[] = {
        PointClampSampler(0),
        PointClampSampler(1)
    };
    D3D12_ROOT_SIGNATURE_DESC signature = {};
    signature.NumParameters = static_cast<UINT>(sizeof(params) / sizeof(params[0]));
    signature.pParameters = params;
    signature.NumStaticSamplers = 2;
    signature.pStaticSamplers = samplers;
    signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> encoded, errors;
    const HRESULT encodedResult = D3D12SerializeRootSignature(
        &signature, D3D_ROOT_SIGNATURE_VERSION_1, &encoded, &errors);
    if (!state.CheckResult(encodedResult, "D3D12SerializeRootSignature"))
    {
        if (errors)
            state.error += ": " + std::string(
                static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
        return false;
    }
    if (!state.CheckResult(device->CreateRootSignature(0, encoded->GetBufferPointer(),
                           encoded->GetBufferSize(), IID_PPV_ARGS(&state.root)),
                           "Create W3D mesh root signature"))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = MaxTextures;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!state.CheckResult(device->CreateDescriptorHeap(
                          &heapDesc, IID_PPV_ARGS(&state.texturesHeap)),
                          "Create W3D texture descriptor heap"))
        return false;
    state.descriptorStride = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    state.shaderPath = shaderSource;
    state.presentation = &presentation;
    return true;
}

bool DX12W3DIndexedMeshPass::UploadTextureRGBA8(const std::uint8_t * pixels,
                                                unsigned width, unsigned height,
                                                unsigned & textureId)
{
    Impl & state = *impl_;
    state.error.clear();
    if (!state.presentation || !state.presentation->ActiveCommandList() || !pixels ||
        width == 0 || height == 0 ||
        width > std::numeric_limits<UINT>::max() / 4 ||
        state.textures.size() >= MaxTextures)
    {
        state.error = "Texture upload requires an active frame, RGBA8 data and descriptor capacity";
        return false;
    }

    ID3D12Device * device = state.Device();
    ID3D12GraphicsCommandList * commands = state.presentation->ActiveCommandList();
    const auto textureDescription = Texture(width, height);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT64 uploadBytes = 0;
    device->GetCopyableFootprints(&textureDescription, 0, 1, 0,
                                  &layout, nullptr, nullptr, &uploadBytes);
    if (uploadBytes == 0 || uploadBytes > std::numeric_limits<SIZE_T>::max())
    {
        state.error = "Invalid D3D12 texture upload footprint";
        return false;
    }

    Impl::TextureRecord texture;
    if (!state.CheckResult(CreateResource(device, D3D12_HEAP_TYPE_DEFAULT,
                           textureDescription, D3D12_RESOURCE_STATE_COPY_DEST,
                           texture.gpu), "Create W3D GPU texture") ||
        !state.CheckResult(CreateResource(device, D3D12_HEAP_TYPE_UPLOAD,
                           Buffer(uploadBytes), D3D12_RESOURCE_STATE_GENERIC_READ,
                           texture.upload), "Create W3D texture staging buffer"))
        return false;

    void * mapped = nullptr;
    if (!state.CheckResult(texture.upload->Map(0, nullptr, &mapped), "Map texture staging"))
        return false;
    std::memset(mapped, 0, static_cast<SIZE_T>(uploadBytes));
    const SIZE_T rowBytes = static_cast<SIZE_T>(width) * 4;
    auto * dst = static_cast<std::uint8_t *>(mapped);
    for (unsigned y = 0; y < height; ++y)
        std::memcpy(dst + static_cast<SIZE_T>(layout.Footprint.RowPitch) * y,
                    pixels + rowBytes * y, rowBytes);
    texture.upload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION to = {}, from = {};
    to.pResource = texture.gpu.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource = texture.upload.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint = layout;
    commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    const auto barrier = Barrier(texture.gpu.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commands->ResourceBarrier(1, &barrier);

    const unsigned slot = static_cast<unsigned>(state.textures.size());
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = TextureFormat;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor =
        state.texturesHeap->GetCPUDescriptorHandleForHeapStart();
    descriptor.ptr += static_cast<SIZE_T>(slot) * state.descriptorStride;
    device->CreateShaderResourceView(texture.gpu.Get(), &srv, descriptor);
    state.textures.push_back(std::move(texture));
    textureId = slot;
    return true;
}

bool DX12W3DIndexedMeshPass::Draw(const DX12W3DIndexedMesh & mesh,
                                 unsigned firstIndex, unsigned indexCount,
                                 const RenderTextureCombinerKey & combiner,
                                 const RenderBlendState & blend,
                                 const float worldViewProjection[16],
                                 unsigned texture0, unsigned texture1)
{
    Impl & state = *impl_;
    state.error.clear();
    if (!state.presentation || !state.presentation->ActiveCommandList() ||
        !mesh.IsReady() || !worldViewProjection || indexCount == 0 ||
        indexCount % 3 != 0 || firstIndex > mesh.IndexCount() ||
        indexCount > mesh.IndexCount() - firstIndex ||
        texture0 >= state.textures.size() || texture1 >= state.textures.size())
    {
        state.error = "Draw requires active DX12 frame, uploaded indexed mesh, textures and valid submesh range";
        return false;
    }
    if (combiner.Requires_Bump_Shader() || !blend.colorWriteEnabled ||
        (blend.blendEnabled &&
         (blend.source != RENDER_BLEND_SRC_ALPHA ||
          blend.destination != RENDER_BLEND_INV_SRC_ALPHA)))
    {
        state.error = "Unsupported W3D material: bump mapping, nonstandard blending or disabled color writes";
        return false;
    }
    const UINT key = combiner.Encode();
    ID3D12PipelineState * pipeline = state.GetPipeline(key, blend.blendEnabled);
    if (!pipeline)
        return false;

    DrawConstants constants = {};
    std::memcpy(constants.wvp, worldViewProjection, sizeof(constants.wvp));
    std::fill(std::begin(constants.factor), std::end(constants.factor), 1.0f);
    constants.alphaTest = blend.alphaTestEnabled ? 1u : 0u;
    constants.alphaReference = blend.alphaReference;
    constants.alphaCompare = static_cast<UINT>(blend.alphaCompare);

    ID3D12GraphicsCommandList * commands = state.presentation->ActiveCommandList();
    ID3D12DescriptorHeap * heaps[] = {state.texturesHeap.Get()};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetGraphicsRootSignature(state.root.Get());
    commands->SetPipelineState(pipeline);
    commands->SetGraphicsRoot32BitConstants(0, sizeof(constants) / sizeof(UINT),
                                           &constants, 0);

    const auto heapStart = state.texturesHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE t0 = heapStart, t1 = heapStart;
    t0.ptr += static_cast<UINT64>(texture0) * state.descriptorStride;
    t1.ptr += static_cast<UINT64>(texture1) * state.descriptorStride;
    commands->SetGraphicsRootDescriptorTable(1, t0);
    commands->SetGraphicsRootDescriptorTable(2, t1);

    const float width = static_cast<float>(state.presentation->Width());
    const float height = static_cast<float>(state.presentation->Height());
    float left = 0.f, top = 0.f, viewWidth = width, viewHeight = height;
    const float cameraAspect = state.presentation->SourceAspect();
    if (cameraAspect > 0.f && width > 0.f && height > 0.f)
    {
        // The WVP matrix is borrowed from the legacy camera. Rendering it
        // into a differently proportioned DX12 window without pillarboxing
        // would stretch units, buildings, water and terrain.
        if (width / height > cameraAspect)
        {
            viewWidth = height * cameraAspect;
            left = (width - viewWidth) * 0.5f;
        }
        else
        {
            viewHeight = width / cameraAspect;
            top = (height - viewHeight) * 0.5f;
        }
    }
    const D3D12_VIEWPORT viewport = {
        left, top, viewWidth, viewHeight, 0.f, 1.f
    };
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->IASetVertexBuffers(0, 1, &mesh.impl_->vertexView);
    commands->IASetIndexBuffer(&mesh.impl_->indexView);
    commands->DrawIndexedInstanced(indexCount, 1, firstIndex, 0, 0);
    return true;
}

unsigned DX12W3DIndexedMeshPass::PipelineCount() const
{
    return static_cast<unsigned>(impl_->pipelines.size());
}
const char * DX12W3DIndexedMeshPass::LastError() const
{
    return impl_->error.c_str();
}
