/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
// M01/B07: the original W3D IA bindings are submitted directly on the
// selected Renderer command list. This is intentionally independent of the
// older preview's separately allocated DX12W3DIndexedMesh.
#include "DX12W3DNativeDraw.h"
#include "DX12PresentationDevice.h"
#include "DX12W3DRenderTargets.h"
#include "DX12W3DTextureStages.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace
{
    // Embedded HLSL: the game must not require a developer's shader-source
    // directory at runtime. Use the W3D FVF input layout, not preview vertices.
    constexpr char GameShader[] = R"(
cbuffer W3DConstants : register(b0)
{
    row_major float4x4 WVP;
    uint HasTexture0;
    uint HasTexture1;
    uint AlphaTest;
    uint AlphaReference;
};
Texture2D Texture0 : register(t0);
Texture2D Texture1 : register(t1);
SamplerState Sampler0 : register(s0);
SamplerState Sampler1 : register(s1);
struct InVertex
{
#if W3D_POSITION4
    float4 position : POSITION;
#else
    float3 position : POSITION;
#endif
#if W3D_HAS_COLOR
    float4 diffuse : COLOR0;
#endif
#if W3D_HAS_UV0
    float2 uv0 : TEXCOORD0;
#endif
#if W3D_HAS_UV1
    float2 uv1 : TEXCOORD1;
#endif
};
struct OutVertex
{
    float4 position : SV_POSITION;
    float4 diffuse : COLOR0;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
};
OutVertex W3D_VS(InVertex v)
{
    OutVertex o;
    o.position = mul(float4(v.position.xyz, 1.0f), WVP);
#if W3D_HAS_COLOR
    // DX8 D3DCOLOR packs 0xAARRGGBB as little-endian BGRA bytes.
    o.diffuse = v.diffuse.bgra;
#else
    o.diffuse = float4(1,1,1,1);
#endif
#if W3D_HAS_UV0
    o.uv0 = v.uv0;
#else
    o.uv0 = float2(0,0);
#endif
#if W3D_HAS_UV1
    o.uv1 = v.uv1;
#else
    o.uv1 = float2(0,0);
#endif
    return o;
}
float4 W3D_PS(OutVertex v) : SV_TARGET
{
    float4 result = v.diffuse;
    if (HasTexture0 != 0u)
        result *= Texture0.Sample(Sampler0, v.uv0);
    if (HasTexture1 != 0u)
        result *= Texture1.Sample(Sampler1, v.uv1);
    if (AlphaTest != 0u)
        clip(result.a * 255.0f - (float)AlphaReference - 0.5f);
    return result;
}
)";

    D3D12_STATIC_SAMPLER_DESC Sampler(unsigned index)
    {
        D3D12_STATIC_SAMPLER_DESC s = {};
        s.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MipLODBias = 0;
        s.MaxAnisotropy = 1;
        s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        s.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        s.MinLOD = 0;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        s.ShaderRegister = index;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        return s;
    }

    D3D12_BLEND Blend(RenderBlendFactor factor)
    {
        switch (factor)
        {
        case RENDER_BLEND_ZERO: return D3D12_BLEND_ZERO;
        case RENDER_BLEND_ONE: return D3D12_BLEND_ONE;
        case RENDER_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_COLOR;
        case RENDER_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_COLOR;
        case RENDER_BLEND_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
        case RENDER_BLEND_INV_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
        case RENDER_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_COLOR;
        case RENDER_BLEND_DEST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
        case RENDER_BLEND_INV_DEST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
        default: return D3D12_BLEND_ONE;
        }
    }

    D3D12_COMPARISON_FUNC Compare(RenderCompareFunc value)
    {
        if (value < RENDER_COMPARE_NEVER || value > RENDER_COMPARE_ALWAYS)
            return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        return static_cast<D3D12_COMPARISON_FUNC>(static_cast<unsigned>(value) + 1);
    }

    DXGI_FORMAT DepthFormat(ID3D12Resource * depth)
    {
        if (!depth) return DXGI_FORMAT_UNKNOWN;
        const DXGI_FORMAT format = depth->GetDesc().Format;
        if (format == DXGI_FORMAT_R32_TYPELESS || format == DXGI_FORMAT_R32_FLOAT)
            return DXGI_FORMAT_D32_FLOAT;
        if (format == DXGI_FORMAT_R24G8_TYPELESS ||
            format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        return format;
    }

    bool HasSemantic(const DX12W3DVertexLayout & layout,
                     const char * semantic, unsigned index, DXGI_FORMAT required)
    {
        for (unsigned i = 0; i < layout.elementCount; ++i)
            if (std::strcmp(layout.elements[i].SemanticName, semantic) == 0 &&
                layout.elements[i].SemanticIndex == index &&
                layout.elements[i].Format == required)
                return true;
        return false;
    }

    struct DrawConstants
    {
        float wvp[16];
        unsigned texture0;
        unsigned texture1;
        unsigned alphaTest;
        unsigned alphaReference;
    };
    static_assert(sizeof(DrawConstants) == 20 * sizeof(unsigned),
                  "Native DX12 W3D shader constants mismatch");

    bool Compile(const char * entry, const char * profile,
                 const D3D_SHADER_MACRO * defines,
                 ComPtr<ID3DBlob> & result, std::string & error)
    {
        ComPtr<ID3DBlob> diagnostics;
        const HRESULT hr = D3DCompile(GameShader, sizeof(GameShader) - 1,
            "W3DNativeGameDraw", defines, nullptr, entry, profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &result, &diagnostics);
        if (FAILED(hr))
        {
            error = "Compiling native W3D draw shader failed";
            if (diagnostics)
                error.append(": ").append(
                    static_cast<const char *>(diagnostics->GetBufferPointer()),
                    diagnostics->GetBufferSize());
        }
        return SUCCEEDED(hr);
    }
}

struct DX12W3DNativeDraw::Impl
{
    DX12W3DVertexLayout layout = {};
    bool hasVertex = false;
    bool hasIndex = false;
    bool enabled = true;
    unsigned vertices = 0;
    unsigned indices = 0;
    unsigned draws = 0;
    RenderBlendState blend;
    RenderCompareFunc depthCompare = RENDER_COMPARE_LESS_EQUAL;
    bool depthWrite = true;
    RenderCullMode cull = RENDER_CULL_NONE;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3DBlob> pixelShader;
    std::map<std::array<unsigned, 12>, ComPtr<ID3D12PipelineState>> pipelines;
    std::string error;

    bool Fail(const char * description)
    {
        error = description;
        return false;
    }

    bool EnsureRoot(DX12PresentationDevice & frame)
    {
        auto * current = frame.NativeDevice();
        if (root && device.Get() == current)
            return true;
        // Presentation shutdown and submission fences retain previously
        // referenced PSOs and signatures; this cache only owns future draws.
        pipelines.clear();
        pixelShader.Reset();
        root.Reset();
        device = current;
        if (!device)
            return Fail("Cannot initialize game draw without a DX12 device");

        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        D3D12_ROOT_PARAMETER params[3] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = sizeof(DrawConstants) / sizeof(unsigned);
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        for (unsigned i = 0; i != 2; ++i)
        {
            ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[i].NumDescriptors = 1;
            ranges[i].BaseShaderRegister = i;
            params[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i + 1].DescriptorTable.NumDescriptorRanges = 1;
            params[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
            params[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        const D3D12_STATIC_SAMPLER_DESC samplers[2] = {Sampler(0), Sampler(1)};
        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 3;
        desc.pParameters = params;
        desc.NumStaticSamplers = 2;
        desc.pStaticSamplers = samplers;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> serialized, diagnostics;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &serialized, &diagnostics)))
            return Fail("D3D12SerializeRootSignature failed for W3D game drawing");
        if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(),
            serialized->GetBufferSize(), IID_PPV_ARGS(&root))))
            return Fail("Cannot create native W3D game drawing root signature");
        if (!Compile("W3D_PS", "ps_5_1", nullptr, pixelShader, error))
            return false;
        return true;
    }

    ID3D12PipelineState * Pipeline(DX12PresentationDevice & frame,
                                   DXGI_FORMAT color, DXGI_FORMAT depth,
                                   unsigned samples, unsigned sampleQuality)
    {
        const bool color0 = HasSemantic(layout, "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM);
        const bool uv0 = HasSemantic(layout, "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT);
        const bool uv1 = HasSemantic(layout, "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT);
        const bool position4 = HasSemantic(layout, "POSITION", 0,
                                            DXGI_FORMAT_R32G32B32A32_FLOAT);
        // M01 supports rigid W3D XYZ/XYZW and 2-float UVs. R03 supports
        // additional vertex types; skinning/RHW behavior is M02/M03 work.
        if (layout.transformed || !layout.elementCount ||
            (!position4 && !HasSemantic(layout, "POSITION", 0,
                                          DXGI_FORMAT_R32G32B32_FLOAT)))
        {
            Fail("W3D native draw requires a rigid XYZ/XYZW vertex layout");
            return nullptr;
        }
        const auto key = std::array<unsigned, 12>{{
            layout.legacyFVF, static_cast<unsigned>(color), static_cast<unsigned>(depth),
            samples, sampleQuality, static_cast<unsigned>(cull),
            static_cast<unsigned>(depthCompare), depthWrite ? 1u : 0u,
            blend.blendEnabled ? 1u : 0u,
            static_cast<unsigned>(blend.source),
            static_cast<unsigned>(blend.destination),
            (static_cast<unsigned>(blend.colorWriteEnabled) << 1) |
            static_cast<unsigned>(blend.alphaTestEnabled)
        }};
        const auto existing = pipelines.find(key);
        if (existing != pipelines.end())
            return existing->second.Get();

        const D3D_SHADER_MACRO macros[] = {
            {"W3D_HAS_COLOR", color0 ? "1" : "0"},
            {"W3D_HAS_UV0", uv0 ? "1" : "0"},
            {"W3D_HAS_UV1", uv1 ? "1" : "0"},
            {"W3D_POSITION4", position4 ? "1" : "0"},
            {nullptr, nullptr}
        };
        ComPtr<ID3DBlob> vertexShader;
        if (!Compile("W3D_VS", "vs_5_1", macros, vertexShader, error))
            return nullptr;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root.Get();
        desc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        desc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
        desc.InputLayout = layout.NativeInputLayout();
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = cull == RENDER_CULL_NONE ?
            D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
        desc.RasterizerState.FrontCounterClockwise = cull == RENDER_CULL_CW;
        desc.RasterizerState.DepthClipEnable = TRUE;
        auto & target = desc.BlendState.RenderTarget[0];
        target.BlendEnable = blend.blendEnabled;
        target.SrcBlend = Blend(blend.source);
        target.DestBlend = Blend(blend.destination);
        target.BlendOp = D3D12_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        target.LogicOp = D3D12_LOGIC_OP_NOOP;
        target.RenderTargetWriteMask =
            blend.colorWriteEnabled ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;
        desc.DepthStencilState.DepthEnable = depth != DXGI_FORMAT_UNKNOWN &&
            depthCompare != RENDER_COMPARE_ALWAYS;
        desc.DepthStencilState.DepthWriteMask = depthWrite ?
            D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState.DepthFunc = Compare(depthCompare);
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = color;
        desc.DSVFormat = depth;
        desc.SampleDesc.Count = samples;
        desc.SampleDesc.Quality = sampleQuality;
        ComPtr<ID3D12PipelineState> result;
        if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&result))))
        {
            Fail("CreateGraphicsPipelineState failed for original W3D FVF");
            return nullptr;
        }
        return pipelines.emplace(key, std::move(result)).first->second.Get();
    }
};

DX12W3DNativeDraw::DX12W3DNativeDraw() : impl_(new Impl) {}
DX12W3DNativeDraw::~DX12W3DNativeDraw() = default;
void DX12W3DNativeDraw::Reset() { impl_.reset(new Impl); }
void DX12W3DNativeDraw::BeginFrame() { BindVertex(nullptr, 0); BindIndex(0); impl_->draws = 0; }
void DX12W3DNativeDraw::BindVertex(const DX12W3DVertexLayout * layout, unsigned count)
{
    impl_->hasVertex = layout != nullptr && layout->elementCount > 0;
    impl_->layout = impl_->hasVertex ? *layout : DX12W3DVertexLayout{};
    impl_->vertices = count;
}
void DX12W3DNativeDraw::BindIndex(unsigned count)
{
    impl_->hasIndex = count > 0;
    impl_->indices = count;
}
void DX12W3DNativeDraw::SetBlend(const RenderBlendState & state) { impl_->blend = state; }
void DX12W3DNativeDraw::SetDepthCompare(RenderCompareFunc c) { impl_->depthCompare = c; }
void DX12W3DNativeDraw::SetDepthWrite(bool enabled) { impl_->depthWrite = enabled; }
void DX12W3DNativeDraw::SetCull(RenderCullMode cull) { impl_->cull = cull; }
void DX12W3DNativeDraw::SetAlphaTest(bool enabled) { impl_->blend.alphaTestEnabled = enabled; }
void DX12W3DNativeDraw::SetDrawEnabled(bool enabled) { impl_->enabled = enabled; }
bool DX12W3DNativeDraw::IsDrawEnabled() const { return impl_->enabled; }
unsigned DX12W3DNativeDraw::DrawCount() const { return impl_->draws; }
unsigned DX12W3DNativeDraw::PipelineCount() const { return static_cast<unsigned>(impl_->pipelines.size()); }
const char * DX12W3DNativeDraw::LastError() const { return impl_->error.c_str(); }

bool DX12W3DNativeDraw::Draw(DX12PresentationDevice & frame,
                             DX12W3DRenderTargets & targets,
                             DX12W3DTextureStages & stages, const float wvp[16],
                             bool strip, unsigned firstIndex, unsigned primitives,
                             unsigned minVertex, unsigned vertexCount)
{
    Impl & s = *impl_;
    s.error.clear();
    if (!s.enabled)
        return true;
    if (!frame.IsRecording() || !frame.ActiveCommandList() || !wvp ||
        !s.hasVertex || !s.hasIndex || !primitives)
        return s.Fail("W3D draw needs an active frame, vertex and index bindings");
    const unsigned long long usedIndices = strip ?
        static_cast<unsigned long long>(primitives) + 2ull :
        static_cast<unsigned long long>(primitives) * 3ull;
    if (usedIndices > s.indices || firstIndex > s.indices - usedIndices ||
        (vertexCount && (minVertex > s.vertices ||
                         vertexCount > s.vertices - minVertex)))
        return s.Fail("W3D indexed draw exceeds bound IA geometry slice");

    auto * colorTarget = targets.BoundColorResource(frame);
    auto * depthTarget = targets.BoundDepthResource(frame);
    if (!colorTarget)
        return s.Fail("M01 color draw requires a bound R07 render target");
    const auto desc = colorTarget->GetDesc();
    if (desc.SampleDesc.Count == 0 || desc.Format == DXGI_FORMAT_UNKNOWN)
        return s.Fail("Invalid native W3D render target format");
    const auto depth = DepthFormat(depthTarget);
    if (!s.EnsureRoot(frame))
        return false;
    auto * pipeline = s.Pipeline(frame, desc.Format, depth,
                                 desc.SampleDesc.Count, desc.SampleDesc.Quality);
    if (!pipeline)
        return false;

    const bool canTexture0 = HasSemantic(s.layout, "TEXCOORD", 0,
                                        DXGI_FORMAT_R32G32_FLOAT);
    const bool canTexture1 = HasSemantic(s.layout, "TEXCOORD", 1,
                                        DXGI_FORMAT_R32G32_FLOAT);
    DrawConstants constants = {};
    std::memcpy(constants.wvp, wvp, sizeof(constants.wvp));
    constants.texture0 = stages.Bound(0) && canTexture0 ? 1u : 0u;
    constants.texture1 = stages.Bound(1) && canTexture1 ? 1u : 0u;
    constants.alphaTest = s.blend.alphaTestEnabled ? 1u : 0u;
    constants.alphaReference = s.blend.alphaReference;

    auto * cmd = frame.ActiveCommandList();
    cmd->SetGraphicsRootSignature(s.root.Get());
    cmd->SetPipelineState(pipeline);
    cmd->SetGraphicsRoot32BitConstants(0, sizeof(constants) / sizeof(unsigned), &constants, 0);
    if (!stages.BindForDraw(frame, 1))
        return s.Fail(stages.LastError());
    if (!frame.RetainFrameObject(pipeline) || !frame.RetainFrameObject(s.root.Get()))
        return s.Fail(frame.LastError());
    cmd->IASetPrimitiveTopology(strip ?
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawIndexedInstanced(static_cast<UINT>(usedIndices), 1,
                              firstIndex, frame.BoundBaseVertexIndex(), 0);
    ++s.draws;
    return true;
}
