// Standalone W3D Direct3D 12 GPU smoke test. It deliberately does NOT
// change the game's DX8 backend or require a dedicated physical GPU.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
constexpr UINT Width = 64;
constexpr UINT Height = 64;
constexpr UINT Pitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

void Check(HRESULT hr, const char * action)
{
    if (FAILED(hr)) {
        char code[32];
        sprintf_s(code, "0x%08X", static_cast<unsigned>(hr));
        throw std::runtime_error(std::string(action) + ": " + code);
    }
}

D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES p = {};
    p.Type = type;
    p.CreationNodeMask = p.VisibleNodeMask = 1;
    return p;
}

D3D12_RESOURCE_DESC Buffer(UINT64 bytes)
{
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

D3D12_RESOURCE_DESC Texture(UINT w, UINT h, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Flags = flags;
    return d;
}

ComPtr<ID3D12Resource> Resource(ID3D12Device * device, D3D12_HEAP_TYPE type,
                                const D3D12_RESOURCE_DESC & desc,
                                D3D12_RESOURCE_STATES state,
                                const D3D12_CLEAR_VALUE * clear = nullptr)
{
    ComPtr<ID3D12Resource> resource;
    auto heap = Heap(type);
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
          &desc, state, clear, IID_PPV_ARGS(&resource)), "CreateCommittedResource");
    return resource;
}

D3D12_RESOURCE_BARRIER Barrier(ID3D12Resource * resource,
                               D3D12_RESOURCE_STATES before,
                               D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}

ComPtr<ID3DBlob> Compile(const wchar_t * shader, const char * entry,
                         const char * target)
{
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompileFromFile(shader, nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr) && errors)
        std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << '\n';
    Check(hr, "D3DCompileFromFile");
    return code;
}

ComPtr<ID3DBlob> CompileSolid(const char * code, const char * entry,
                             const char * target)
{
    ComPtr<ID3DBlob> shader, errors;
    const HRESULT hr = D3DCompile(
        code, std::strlen(code), "minimal-dx12-probe", nullptr, nullptr,
        entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors);
    if (FAILED(hr) && errors)
        std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << '\n';
    Check(hr, "D3DCompile(minimal shader)");
    return shader;
}

struct Vertex {
    float pos[3];
    float color[4];
    float uv0[2];
    float uv1[2];
};

// GPU root CBV addresses must be 256-byte aligned.
struct alignas(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) Constants {
    float wvp[16] = {};
    float textureFactor[4] = {1.f, 1.f, 1.f, 1.f};
    unsigned combiner = 0x10101193u; // stage 0: texture * diffuse
    unsigned alphaTest = 1;
    unsigned alphaReference = 0x60;
    unsigned alphaCompare = 6; // GREATER_EQUAL from Renderer.h
};
static_assert(sizeof(Constants) == 256, "CBV layout/alignment");

void PrintDiagnostics(ID3D12Device * device)
{
    const HRESULT removed = device->GetDeviceRemovedReason();
    std::cerr << "DX12 GetDeviceRemovedReason: 0x"
              << std::hex << static_cast<unsigned>(removed) << std::dec << '\n';
    ComPtr<ID3D12InfoQueue> info;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&info)))) {
        std::cerr << "D3D12 debug information queue unavailable\n";
        return;
    }
    const UINT64 count = info->GetNumStoredMessagesAllowedByRetrievalFilter();
    const UINT64 start = count > 45 ? count - 45 : 0;
    for (UINT64 i = start; i < count; ++i) {
        SIZE_T size = 0;
        if (FAILED(info->GetMessage(i, nullptr, &size)) || !size) continue;
        std::vector<BYTE> storage(size);
        auto * message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        if (SUCCEEDED(info->GetMessage(i, message, &size)))
            std::cerr << "DX12 debug [" << message->ID << "]: "
                      << message->pDescription << '\n';
    }
}

void Run(const wchar_t * shader, bool clearOnly, bool untextured, bool solid,
         bool w3dVertexOnly, bool basicTexture)
{
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        std::cout << "D3D12 debug layer enabled\n";
    } else {
        std::cout << "D3D12 debug layer is not installed\n";
    }
    // WARP gives a functional software D3D12 device on Windows CI.
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
    ComPtr<IDXGIAdapter> adapter;
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
    DXGI_ADAPTER_DESC adapterDesc = {};
    Check(adapter->GetDesc(&adapterDesc), "IDXGIAdapter::GetDesc");
    std::wcout << L"WARP adapter: " << adapterDesc.Description << L'\n';
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
          IID_PPV_ARGS(&device)), "D3D12CreateDevice");

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)),
          "CreateCommandQueue");
    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
          IID_PPV_ARGS(&allocator)), "CreateCommandAllocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
          allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "CreateCommandList");

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 2;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors = 2;
    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &ranges[1];
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC sigDesc = {};
    sigDesc.NumParameters = 3;
    sigDesc.pParameters = params;
    sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature, signatureErrors;
    HRESULT hr = D3D12SerializeRootSignature(
        &sigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &signature, &signatureErrors);
    if (FAILED(hr) && signatureErrors)
        std::cerr << static_cast<const char *>(signatureErrors->GetBufferPointer()) << '\n';
    Check(hr, "D3D12SerializeRootSignature");
    ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(),
          signature->GetBufferSize(), IID_PPV_ARGS(&root)), "CreateRootSignature");

    // This deliberately bypasses W3D combiners and all shader resources:
    // any device removal here is a generic D3D12 draw/environment problem.
    static const char * const minimalShader = R"(
        float4 W3D_VS(float3 position : POSITION) : SV_POSITION {
            return float4(position, 1.0f);
        }
        float4 W3D_PS() : SV_TARGET0 {
            return float4(1.0f, 0.0f, 0.0f, 1.0f);
        }
    )";
    // The basic texture test isolates texture resource upload and sampling
    // from W3D's more complicated runtime fixed-function combiner shader.
    static const char * const basicTextureShader = R"(
        Texture2D sampleTexture : register(t0);
        SamplerState textureSampler : register(s0);
        struct W3DVertexOutput {
            float4 position : SV_POSITION;
            float4 diffuse : COLOR0;
            float2 uv0 : TEXCOORD0;
            float2 uv1 : TEXCOORD1;
        };
        float4 W3D_PS(W3DVertexOutput input) : SV_TARGET0 {
            return saturate(sampleTexture.Sample(textureSampler, input.uv0)
                            * input.diffuse);
        }
    )";
    auto vs = solid ? CompileSolid(minimalShader, "W3D_VS", "vs_5_1")
                    : Compile(shader, "W3D_VS", "vs_5_1");
    auto ps = (solid || w3dVertexOnly)
        ? CompileSolid(minimalShader, "W3D_PS", "ps_5_1")
        : basicTexture
        ? CompileSolid(basicTextureShader, "W3D_PS", "ps_5_1")
        : Compile(shader, "W3D_PS", "ps_5_1");

    D3D12_INPUT_ELEMENT_DESC inputs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         static_cast<UINT>(offsetof(Vertex, pos)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         static_cast<UINT>(offsetof(Vertex, color)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         static_cast<UINT>(offsetof(Vertex, uv0)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0,
         static_cast<UINT>(offsetof(Vertex, uv1)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = root.Get();
    psoDesc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    psoDesc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    psoDesc.InputLayout = {inputs, _countof(inputs)};
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    auto & rtBlend = psoDesc.BlendState.RenderTarget[0];
    rtBlend.SrcBlend = D3D12_BLEND_ONE;
    rtBlend.DestBlend = D3D12_BLEND_ZERO;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pso;
    Check(device->CreateGraphicsPipelineState(
          &psoDesc, IID_PPV_ARGS(&pso)), "CreateGraphicsPipelineState");
    Check(device->GetDeviceRemovedReason(), "device health after PSO creation");

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear.Color[3] = 1.f;
    auto target = Resource(device.Get(), D3D12_HEAP_TYPE_DEFAULT,
        Texture(Width, Height, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear);
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Check(device->CreateDescriptorHeap(
          &rtvDesc, IID_PPV_ARGS(&rtvHeap)), "RTV heap");
    const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target.Get(), nullptr, rtv);

    // A real texture upload, then bind the same texture at t0 and t1.
    auto texture = Resource(device.Get(), D3D12_HEAP_TYPE_DEFAULT,
        Texture(1, 1, D3D12_RESOURCE_FLAG_NONE),
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto texUpload = Resource(device.Get(), D3D12_HEAP_TYPE_UPLOAD,
        Buffer(Pitch), D3D12_RESOURCE_STATE_GENERIC_READ);
    void * mapped = nullptr;
    Check(texUpload->Map(0, nullptr, &mapped), "Map texture upload");
    std::memset(mapped, 0, Pitch);
    const BYTE rgba[] = {128, 255, 255, 255};
    std::memcpy(mapped, rgba, sizeof(rgba));
    texUpload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
    src.pResource = texUpload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = 1;
    src.PlacedFootprint.Footprint.Height = 1;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = Pitch;
    dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    auto transition = Barrier(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (!clearOnly && !untextured && !solid && !w3dVertexOnly) {
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        list->ResourceBarrier(1, &transition);
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 2;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    Check(device->CreateDescriptorHeap(
          &heapDesc, IID_PPV_ARGS(&srvHeap)), "SRV heap");
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    auto srvHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    const UINT srvSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (int i = 0; i < 2; ++i) {
        device->CreateShaderResourceView(texture.Get(), &srv, srvHandle);
        srvHandle.ptr += srvSize;
    }

    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    ComPtr<ID3D12DescriptorHeap> samplerHeap;
    Check(device->CreateDescriptorHeap(
          &heapDesc, IID_PPV_ARGS(&samplerHeap)), "sampler heap");
    D3D12_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    auto samplerHandle = samplerHeap->GetCPUDescriptorHandleForHeapStart();
    const UINT samplerSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    for (int i = 0; i < 2; ++i) {
        device->CreateSampler(&sampler, samplerHandle);
        samplerHandle.ptr += samplerSize;
    }

    const Vertex vertices[] = {
        {{-0.85f, -0.85f, 0.f}, {1.f, 0.25f, 0.5f, 1.f}, {0.f, 0.f}, {0.f, 0.f}},
        {{ 0.00f,  0.85f, 0.f}, {1.f, 0.25f, 0.5f, 1.f}, {0.f, 0.f}, {0.f, 0.f}},
        {{ 0.85f, -0.85f, 0.f}, {1.f, 0.25f, 0.5f, 1.f}, {0.f, 0.f}, {0.f, 0.f}}
    };
    auto vertexBuffer = Resource(device.Get(), D3D12_HEAP_TYPE_UPLOAD,
        Buffer(sizeof(vertices)), D3D12_RESOURCE_STATE_GENERIC_READ);
    Check(vertexBuffer->Map(0, nullptr, &mapped), "Map vertex buffer");
    std::memcpy(mapped, vertices, sizeof(vertices));
    vertexBuffer->Unmap(0, nullptr);
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vbv.SizeInBytes = sizeof(vertices);
    vbv.StrideInBytes = sizeof(Vertex);

    Constants constants;
    constants.wvp[0] = constants.wvp[5] =
        constants.wvp[10] = constants.wvp[15] = 1.f;
    if (untextured) {
        // COLOROP=DISABLE on both stages: the pixel shader should return
        // interpolated diffuse vertex color without sampling the texture.
        constants.combiner = 0x10101010u;
        constants.alphaTest = 0;
    }
    auto cbv = Resource(device.Get(), D3D12_HEAP_TYPE_UPLOAD,
        Buffer(sizeof(constants)), D3D12_RESOURCE_STATE_GENERIC_READ);
    Check(cbv->Map(0, nullptr, &mapped), "Map constant buffer");
    std::memcpy(mapped, &constants, sizeof(constants));
    cbv->Unmap(0, nullptr);

    const D3D12_VIEWPORT viewport = {0.f, 0.f, float(Width), float(Height), 0.f, 1.f};
    const D3D12_RECT rect = {0, 0, LONG(Width), LONG(Height)};
    ID3D12DescriptorHeap * heaps[] = {srvHeap.Get(), samplerHeap.Get()};
    list->SetDescriptorHeaps(_countof(heaps), heaps);
    list->SetGraphicsRootSignature(root.Get());
    list->SetPipelineState(pso.Get());
    list->SetGraphicsRootConstantBufferView(0, cbv->GetGPUVirtualAddress());
    list->SetGraphicsRootDescriptorTable(
        1, srvHeap->GetGPUDescriptorHandleForHeapStart());
    list->SetGraphicsRootDescriptorTable(
        2, samplerHeap->GetGPUDescriptorHandleForHeapStart());
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &rect);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const float black[] = {0.f, 0.f, 0.f, 1.f};
    list->ClearRenderTargetView(rtv, black, 0, nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &vbv);
    if (!clearOnly)
        list->DrawInstanced(3, 1, 0, 0);

    transition = Barrier(target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->ResourceBarrier(1, &transition);
    auto readback = Resource(device.Get(), D3D12_HEAP_TYPE_READBACK,
        Buffer(UINT64(Pitch) * Height), D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION readDst = {}, renderSrc = {};
    readDst.pResource = readback.Get();
    readDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    readDst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    readDst.PlacedFootprint.Footprint.Width = Width;
    readDst.PlacedFootprint.Footprint.Height = Height;
    readDst.PlacedFootprint.Footprint.Depth = 1;
    readDst.PlacedFootprint.Footprint.RowPitch = Pitch;
    renderSrc.pResource = target.Get();
    renderSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&readDst, 0, 0, 0, &renderSrc, nullptr);
    Check(list->Close(), "Close command list");

    // Create synchronization objects before submitting GPU commands so
    // device removal can be reported with the debug messages from the draw.
    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
          IID_PPV_ARGS(&fence)), "CreateFence");
    Check(device->GetDeviceRemovedReason(), "device health before GPU submission");
    ID3D12CommandList * submitted[] = {list.Get()};
    queue->ExecuteCommandLists(1, submitted);
    HRESULT signalResult = queue->Signal(fence.Get(), 1);
    if (FAILED(signalResult)) {
        PrintDiagnostics(device.Get());
        Check(signalResult, "Queue Signal");
    }
    if (fence->GetCompletedValue() < 1) {
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) throw std::runtime_error("CreateEventW failed");
        hr = fence->SetEventOnCompletion(1, event);
        if (FAILED(hr)) {
            CloseHandle(event);
            Check(hr, "SetEventOnCompletion");
        }
        const DWORD result = WaitForSingleObject(event, 30000);
        CloseHandle(event);
        if (result != WAIT_OBJECT_0) {
            PrintDiagnostics(device.Get());
            throw std::runtime_error("WARP queue timeout");
        }
    }

    const HRESULT gpuStatus = device->GetDeviceRemovedReason();
    if (FAILED(gpuStatus)) {
        PrintDiagnostics(device.Get());
        Check(gpuStatus, "DX12 GPU draw");
    }

    const D3D12_RANGE range = {0, SIZE_T(Pitch) * Height};
    Check(readback->Map(0, &range, &mapped), "Map readback");
    const auto * pixels = static_cast<const BYTE *>(mapped);
    const auto center = pixels + (Height / 2) * Pitch + (Width / 2) * 4;
    const std::array<BYTE, 4> inside = {center[0], center[1], center[2], center[3]};
    const std::array<BYTE, 4> outside = {pixels[0], pixels[1], pixels[2], pixels[3]};
    readback->Unmap(0, nullptr);

    std::cout << "D3D12 WARP triangle center: " << unsigned(inside[0])
              << ',' << unsigned(inside[1]) << ',' << unsigned(inside[2])
              << ',' << unsigned(inside[3]) << '\n';
    auto closeTo = [](BYTE actual, int expected) {
        return std::abs(int(actual) - expected) <= 10;
    };
    // The clear-only test isolates basic GPU submission/copy from texture
    // uploads, pixel shading and drawing. It must produce the clear color.
    const bool insideMatches = clearOnly
        ? (closeTo(inside[0], 0) && closeTo(inside[1], 0) &&
           closeTo(inside[2], 0) && closeTo(inside[3], 255))
        : (solid || w3dVertexOnly)
        ? (closeTo(inside[0], 255) && closeTo(inside[1], 0) &&
           closeTo(inside[2], 0) && closeTo(inside[3], 255))
        : untextured
        ? (closeTo(inside[0], 255) && closeTo(inside[1], 64) &&
           closeTo(inside[2], 128) && closeTo(inside[3], 255))
        : (closeTo(inside[0], 128) && closeTo(inside[1], 64) &&
           closeTo(inside[2], 128) && closeTo(inside[3], 255));
    if (!(insideMatches && closeTo(outside[0], 0) &&
          closeTo(outside[1], 0) && closeTo(outside[2], 0) &&
          closeTo(outside[3], 255)))
        throw std::runtime_error("D3D12 WARP pixel verification failed");
    const char * mode = clearOnly ? "clear/readback"
        : (solid ? "minimal solid-color draw"
            : (w3dVertexOnly ? "W3D vertex with minimal pixel shader"
                : (basicTexture ? "basic texture-modulated draw"
                    : (untextured ? "untextured W3D draw" : "textured W3D draw"))));
    std::cout << "D3D12 WARP " << mode << ": PASS\n";
}
}

int wmain(int argc, wchar_t ** argv)
{
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: W3DDX12Probe <shader.hlsl> [--clear-only|--untextured|--solid|--w3d-vs|--basic-texture]\n";
        return 2;
    }
    try {
        const bool clearOnly = argc == 3 && std::wcscmp(argv[2], L"--clear-only") == 0;
        const bool untextured = argc == 3 && std::wcscmp(argv[2], L"--untextured") == 0;
        const bool solid = argc == 3 && std::wcscmp(argv[2], L"--solid") == 0;
        const bool w3dVertexOnly = argc == 3 && std::wcscmp(argv[2], L"--w3d-vs") == 0;
        const bool basicTexture = argc == 3 && std::wcscmp(argv[2], L"--basic-texture") == 0;
        if (argc == 3 && !clearOnly && !untextured && !solid && !w3dVertexOnly && !basicTexture)
            throw std::runtime_error("Unknown mode");
        Run(argv[1], clearOnly, untextured, solid, w3dVertexOnly, basicTexture);
        return 0;
    } catch (const std::exception & e) {
        std::cerr << "DX12 WARP probe failure: " << e.what() << '\n';
        return 1;
    }
}
