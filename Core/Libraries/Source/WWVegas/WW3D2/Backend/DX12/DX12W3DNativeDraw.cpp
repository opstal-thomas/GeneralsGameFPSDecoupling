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
    uint AlphaCompare;
    float3 AlphaPadding;
    float4 MaterialColor;
    float4 TextureFactor;
    row_major float4x4 NormalToWorld;
    float4 MaterialAmbient;
    float4 MaterialEmissive;
    float4 SceneAmbient;
    float4 LightDirection[4];
    float4 LightDiffuse[4];
    uint LightingEnabled;
    uint LightCount;
    uint AmbientSource;
    uint DiffuseSource;
    uint EmissiveSource;
    uint LightingReserved0;
    uint LightingReserved1;
    uint LightingReserved2;
    row_major float4x4 World;
    row_major float4x4 View;
    float4 MaterialSpecular;
    float4 SpecularProperties;
    float4 LightSpecular[4];
    float4 LightPosition[4];
    float4 LightSpotDirection[4];
    float4 LightAttenuation[4];
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
#if W3D_HAS_SPECULAR
    float4 specular : COLOR1;
#endif
#if W3D_HAS_NORMAL
    float3 normal : NORMAL;
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
    float4 specular : COLOR1;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
};
float4 W3DColorSource(uint source, float4 material, float4 color1, float4 color2)
{
    return source == 1u ? color1 : (source == 2u ? color2 : material);
}
OutVertex W3D_VS(InVertex v)
{
    OutVertex o;
    o.position = mul(float4(v.position.xyz, 1.0f), WVP);
    // DX8 D3DCOLOR stores 0xAARRGGBB in little-endian BGRA bytes.
#if W3D_HAS_COLOR
    float4 color1 = v.diffuse.bgra;
#else
    float4 color1 = float4(1,1,1,1);
#endif
#if W3D_HAS_SPECULAR
    float4 color2 = v.specular.bgra;
#else
    float4 color2 = float4(0,0,0,1);
#endif
    if (LightingEnabled != 0u)
    {
        const float3 ambientMaterial =
            W3DColorSource(AmbientSource, MaterialAmbient, color1, color2).rgb;
        const float3 diffuseMaterial =
            W3DColorSource(DiffuseSource, MaterialColor, color1, color2).rgb;
        const float3 emissiveMaterial =
            W3DColorSource(EmissiveSource, MaterialEmissive, color1, color2).rgb;
        float3 lit = ambientMaterial * SceneAmbient.rgb + emissiveMaterial;
        float3 spec = float3(0, 0, 0);
#if W3D_HAS_NORMAL
        // Row-vector inverse-transpose handles nonuniform model scale.
        float3 normalWorld = normalize(mul(float4(v.normal, 0.f), NormalToWorld).xyz);
        float3 positionWorld = mul(float4(v.position.xyz, 1.f), World).xyz;
        float3 positionView = mul(float4(positionWorld, 1.f), View).xyz;
        float3 eyeVector = -positionView;
        float eyeSq = dot(eyeVector, eyeVector);
        float3 eyeDir = eyeSq > 1.e-8f ?
            eyeVector * rsqrt(eyeSq) : float3(0, 0, 1);
        float3 normalView = normalize(mul(float4(normalWorld, 0.f), View).xyz);
        [unroll]
        for (uint index = 0u; index < 4u; ++index)
        {
            if (index < LightCount)
            {
                float3 lightDirectionWorld = LightDirection[index].xyz;
                float attenuation = 1.f;
                uint type = (uint)LightDirection[index].w;
                if (type != 0u)
                {
                    float3 towardLight = LightPosition[index].xyz - positionWorld;
                    float distance = length(towardLight);
                    lightDirectionWorld = distance > 1.e-5f ?
                        towardLight / distance : float3(0, 0, 1);
                    if (LightAttenuation[index].z > 0.5f)
                    {
                        float nearRange = LightAttenuation[index].x;
                        float farRange = LightAttenuation[index].y;
                        attenuation *= farRange <= nearRange + 1.e-5f ?
                            (distance <= farRange ? 1.f : 0.f) :
                            saturate((farRange - distance) / (farRange - nearRange));
                    }
                    if (type == 2u)
                    {
                        float outerCos = min(LightAttenuation[index].w, 0.9999f);
                        float coneCos = dot(normalize(LightSpotDirection[index].xyz),
                                            -lightDirectionWorld);
                        attenuation *= saturate((coneCos - outerCos) /
                                               max(1.e-4f, 1.f - outerCos));
                    }
                }
                float3 lightDir = normalize(lightDirectionWorld);
                float lambert = saturate(dot(normalWorld, lightDir));
                lit += lambert * attenuation * LightDiffuse[index].rgb * diffuseMaterial;
                if (SpecularProperties.y > 0.5f && lambert > 0.f)
                {
                    float3 lightView = normalize(mul(float4(lightDir, 0.f), View).xyz);
                    float3 halfwayVector = lightView + eyeDir;
                    float halfwaySq = dot(halfwayVector, halfwayVector);
                    float3 halfway = halfwaySq > 1.e-8f ?
                        halfwayVector * rsqrt(halfwaySq) : lightView;
                    float gloss = pow(saturate(dot(normalView, halfway)),
                                      max(1.f, SpecularProperties.x));
                    spec += attenuation * gloss *
                            MaterialSpecular.rgb * LightSpecular[index].rgb;
                }
            }
        }
#endif
        o.diffuse = float4(saturate(lit), color1.a * MaterialColor.a);
        o.specular = float4(saturate(spec), 1.f);
    }
    else
    {
#if W3D_HAS_COLOR
        o.diffuse = color1 * float4(1,1,1,MaterialColor.a);
#else
        o.diffuse = MaterialColor;
#endif
        o.specular = SpecularProperties.y > 0.5f ? color2 : float4(0, 0, 0, 1);
    }
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

uint W3DOp(uint channel) { return channel & 15u; }
uint W3DArg1(uint channel) { return (channel >> 4) & 3u; }
uint W3DArg2(uint channel) { return (channel >> 6) & 3u; }
float4 W3DArgument(uint source, float4 current, float4 texel, float4 diffuse)
{
    return source == 1u ? texel :
           source == 2u ? diffuse :
           source == 3u ? TextureFactor : current;
}
float4 W3DCombine(uint operation, float4 a, float4 b, float4 current, float4 texel)
{
    if (operation == 1u) return a;
    if (operation == 2u) return b;
    if (operation == 3u) return saturate(a * b);
    if (operation == 4u) return saturate(2.f * a * b);
    if (operation == 5u) return saturate(a + b);
    if (operation == 6u) return saturate(a + b - a * b);
    if (operation == 7u) return saturate(a - b);
    if (operation == 8u) return saturate(lerp(b, a, texel.a));
    if (operation == 9u) return saturate(lerp(b, a, current.a));
    if (operation == 10u) return saturate(a + b - 0.5f);
    if (operation == 11u) return saturate(2.f * (a + b - 0.5f));
    if (operation == 12u) return saturate(a + a.a * b);
    // BUMP_ENV_MAP is explicitly rejected before recording native draws.
    return current;
}
float4 W3DStage(uint colorChannel, uint alphaChannel, float4 current,
                float4 texel, float4 diffuse)
{
    uint op = W3DOp(colorChannel);
    if (op == 0u) return current;
    float4 a = W3DArgument(W3DArg1(colorChannel), current, texel, diffuse);
    float4 b = W3DArgument(W3DArg2(colorChannel), current, texel, diffuse);
    float4 color = W3DCombine(op, a, b, current, texel);
    uint alphaOp = W3DOp(alphaChannel);
    if (alphaOp == 0u) return float4(color.rgb, current.a);
    float4 aa = W3DArgument(W3DArg1(alphaChannel), current, texel, diffuse);
    float4 ab = W3DArgument(W3DArg2(alphaChannel), current, texel, diffuse);
    float4 alpha = W3DCombine(alphaOp, aa, ab, current, texel);
    return float4(color.rgb, alpha.a);
}

#ifndef W3D_COMBINER_KEY
#define W3D_COMBINER_KEY 0u
#endif
float4 W3D_PS(OutVertex v) : SV_TARGET
{
    // Compile-time specialization: interpreting a dynamic combiner key
    // previously caused WARP device removal in the R06 smoke test.
    const uint key = W3D_COMBINER_KEY;
    const uint color0 = key & 255u;
    const uint alpha0 = (key >> 8) & 255u;
    const uint color1 = (key >> 16) & 255u;
    const uint alpha1 = (key >> 24) & 255u;
    float4 result = v.diffuse;
    if (HasTexture0 != 0u && W3DOp(color0) != 0u)
    {
        float4 texel0 = Texture0.Sample(Sampler0, v.uv0);
        result = W3DStage(color0, alpha0, result, texel0, v.diffuse);
        if (HasTexture1 != 0u && W3DOp(color1) != 0u)
        {
            float4 texel1 = Texture1.Sample(Sampler1, v.uv1);
            result = W3DStage(color1, alpha1, result, texel1, v.diffuse);
        }
    }
    // D3D8 sums specular after the texture-stage cascade.
    result.rgb = saturate(result.rgb + v.specular.rgb);
    if (AlphaTest != 0u)
    {
        float alpha = saturate(result.a);
        float reference = (float)(AlphaReference & 255u) / 255.0f;
        bool passes = AlphaCompare == 0u ? false :
            AlphaCompare == 1u ? alpha < reference :
            AlphaCompare == 2u ? alpha == reference :
            AlphaCompare == 3u ? alpha <= reference :
            AlphaCompare == 4u ? alpha > reference :
            AlphaCompare == 5u ? alpha != reference :
            AlphaCompare == 6u ? alpha >= reference : true;
        clip(passes ? 1.0f : -1.0f);
    }
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

    // In alpha equations D3D8's SRC_COLOR/DEST_COLOR use the same alpha
    // channel as SRC_ALPHA/DEST_ALPHA, not the fixed alpha blend used by
    // the original minimal M01 proof-of-concept.
    D3D12_BLEND AlphaBlend(RenderBlendFactor factor)
    {
        switch (factor)
        {
        case RENDER_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_ALPHA;
        case RENDER_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_ALPHA;
        case RENDER_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_ALPHA;
        default: return Blend(factor);
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
        unsigned alphaCompare;
        unsigned alphaPadding[3];
        float material[4];
        float textureFactor[4];
        float normalToWorld[16];
        float materialAmbient[4];
        float materialEmissive[4];
        float sceneAmbient[4];
        float lightDirection[4][4];
        float lightDiffuse[4][4];
        unsigned lightingEnabled;
        unsigned lightCount;
        unsigned ambientSource;
        unsigned diffuseSource;
        unsigned emissiveSource;
        unsigned reserved[3];
        float world[16];
        float view[16];
        float materialSpecular[4];
        float specularProperties[4];
        float lightSpecular[4][4];
        float lightPosition[4][4];
        float lightSpotDirection[4][4];
        float lightAttenuation[4][4];
    };
    static_assert(sizeof(DrawConstants) == 204 * sizeof(unsigned),
                  "Native DX12 W3D constant buffer does not match shader packing");

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
    RenderBlendOperation equation = RENDER_BLEND_OPERATION_ADD;
    unsigned char writeMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    RenderVertexMaterialDescription material;
    RenderLightingEnvironmentDescription lighting;
    RenderMaterialColorSources colorSources;
    bool vertexLighting = false;
    bool specularEnabled = false;
    RenderCompareFunc depthCompare = RENDER_COMPARE_LESS_EQUAL;
    bool depthWrite = true;
    RenderCullMode cull = RENDER_CULL_NONE;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    RenderTextureCombinerKey combiner;
    unsigned textureFactor = 0xffffffffu;
    std::map<std::array<unsigned, 15>, ComPtr<ID3D12PipelineState>> pipelines;

    Impl()
    {
        const RenderTextureStageChannel base(RENDER_TEXOP_MODULATE,
            RENDER_TEXARG_TEXTURE, RENDER_TEXARG_DIFFUSE);
        combiner.color[0] = base;
        combiner.alpha[0] = base;
    }
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
        root.Reset();
        device = current;
        if (!device)
            return Fail("Cannot initialize game draw without a DX12 device");

        D3D12_DESCRIPTOR_RANGE ranges[3] = {};
        D3D12_ROOT_PARAMETER params[4] = {};
        // Four legacy directional lights plus normal transforms exceed 64
        // DWORD root constants. A CBV is populated from R02's fence-safe
        // upload pool per draw so deferred transparent batches cannot alias.
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
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
        ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        ranges[2].NumDescriptors = 2;
        ranges[2].BaseShaderRegister = 0;
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges = &ranges[2];
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 4;
        desc.pParameters = params;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> serialized, diagnostics;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &serialized, &diagnostics)))
            return Fail("D3D12SerializeRootSignature failed for W3D game drawing");
        if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(),
            serialized->GetBufferSize(), IID_PPV_ARGS(&root))))
            return Fail("Cannot create native W3D game drawing root signature");
        return true;
    }

    ID3D12PipelineState * Pipeline(DX12PresentationDevice & frame,
                                   DXGI_FORMAT color, DXGI_FORMAT depth,
                                   unsigned samples, unsigned sampleQuality)
    {
        const bool color0 = HasSemantic(layout, "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM);
        const bool uv0 = HasSemantic(layout, "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT);
        const bool uv1 = HasSemantic(layout, "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT);
        const bool hasNormals = HasSemantic(layout, "NORMAL", 0,
                                            DXGI_FORMAT_R32G32B32_FLOAT);
        const bool hasSpecular = HasSemantic(layout, "COLOR", 1,
                                             DXGI_FORMAT_R8G8B8A8_UNORM);
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
        const auto key = std::array<unsigned, 15>{{
            layout.legacyFVF, static_cast<unsigned>(color), static_cast<unsigned>(depth),
            samples, sampleQuality, static_cast<unsigned>(cull),
            static_cast<unsigned>(depthCompare), depthWrite ? 1u : 0u,
            blend.blendEnabled ? 1u : 0u,
            static_cast<unsigned>(blend.source),
            static_cast<unsigned>(blend.destination),
            (static_cast<unsigned>(blend.colorWriteEnabled) << 1) |
            static_cast<unsigned>(blend.alphaTestEnabled),
            static_cast<unsigned>(equation),
            static_cast<unsigned>(writeMask),
            combiner.Encode()
        }};
        const auto existing = pipelines.find(key);
        if (existing != pipelines.end())
            return existing->second.Get();

        const D3D_SHADER_MACRO macros[] = {
            {"W3D_HAS_COLOR", color0 ? "1" : "0"},
            {"W3D_HAS_SPECULAR", hasSpecular ? "1" : "0"},
            {"W3D_HAS_NORMAL", hasNormals ? "1" : "0"},
            {"W3D_HAS_UV0", uv0 ? "1" : "0"},
            {"W3D_HAS_UV1", uv1 ? "1" : "0"},
            {"W3D_POSITION4", position4 ? "1" : "0"},
            {nullptr, nullptr}
        };
        ComPtr<ID3DBlob> vertexShader, pixelShader;
        if (!Compile("W3D_VS", "vs_5_1", macros, vertexShader, error))
            return nullptr;
        const std::string keyText = std::to_string(combiner.Encode()) + "u";
        const D3D_SHADER_MACRO pixelMacros[] = {
            {"W3D_COMBINER_KEY", keyText.c_str()},
            {nullptr, nullptr}
        };
        if (!Compile("W3D_PS", "ps_5_1", pixelMacros, pixelShader, error))
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
        target.BlendOp = equation == RENDER_BLEND_OPERATION_REVERSE_SUBTRACT ?
            D3D12_BLEND_OP_REV_SUBTRACT : D3D12_BLEND_OP_ADD;
        target.SrcBlendAlpha = AlphaBlend(blend.source);
        target.DestBlendAlpha = AlphaBlend(blend.destination);
        target.BlendOpAlpha = target.BlendOp;
        target.LogicOp = D3D12_LOGIC_OP_NOOP;
        target.RenderTargetWriteMask = writeMask;
        desc.DepthStencilState.DepthEnable = depth != DXGI_FORMAT_UNKNOWN;
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
void DX12W3DNativeDraw::SetBlend(const RenderBlendState & state)
{
    impl_->blend = state;
    impl_->writeMask = state.colorWriteEnabled ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;
}
void DX12W3DNativeDraw::SetSourceBlend(RenderBlendFactor value)
{
    impl_->blend.source = value;
}
void DX12W3DNativeDraw::SetDestinationBlend(RenderBlendFactor value)
{
    impl_->blend.destination = value;
}
void DX12W3DNativeDraw::SetBlendOperation(RenderBlendOperation value)
{
    impl_->equation = value;
}
void DX12W3DNativeDraw::SetBlendingEnabled(bool enabled)
{
    impl_->blend.blendEnabled = enabled;
}
void DX12W3DNativeDraw::SetColorWriteMask(bool rgbEnabled, bool alphaEnabled)
{
    impl_->writeMask = static_cast<unsigned char>(
        (rgbEnabled ? D3D12_COLOR_WRITE_ENABLE_RED |
                      D3D12_COLOR_WRITE_ENABLE_GREEN |
                      D3D12_COLOR_WRITE_ENABLE_BLUE : 0) |
        (alphaEnabled ? D3D12_COLOR_WRITE_ENABLE_ALPHA : 0));
    impl_->blend.colorWriteEnabled = impl_->writeMask != 0;
}
void DX12W3DNativeDraw::SetMaterial(const RenderVertexMaterialDescription & state)
{
    impl_->material = state;
}
void DX12W3DNativeDraw::SetTextureChannel(
    unsigned stage, bool alpha, const RenderTextureStageChannel & channel)
{
    if (stage < 2u)
    {
        if (alpha) impl_->combiner.alpha[stage] = channel;
        else impl_->combiner.color[stage] = channel;
    }
}
void DX12W3DNativeDraw::SetTextureFactor(unsigned packedARGB)
{
    impl_->textureFactor = packedARGB;
}
void DX12W3DNativeDraw::SetLightingEnabled(bool enabled)
{
    impl_->vertexLighting = enabled;
}
void DX12W3DNativeDraw::SetSpecularEnabled(bool enabled)
{
    impl_->specularEnabled = enabled;
}
void DX12W3DNativeDraw::SetMaterialColorSources(const RenderMaterialColorSources & sources)
{
    impl_->colorSources = sources;
}
void DX12W3DNativeDraw::SetLightingEnvironment(const RenderLightingEnvironmentDescription & lights)
{
    impl_->lighting = lights;
    if (impl_->lighting.lightCount > 4)
        impl_->lighting.lightCount = 4;
}
RenderLightingEnvironmentDescription DX12W3DNativeDraw::GetLightingEnvironment() const
{
    return impl_->lighting;
}
void DX12W3DNativeDraw::SetAmbient(float red, float green, float blue)
{
    impl_->lighting.ambient[0] = red;
    impl_->lighting.ambient[1] = green;
    impl_->lighting.ambient[2] = blue;
}
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
                             const float normalWorld[16],
                             bool strip, unsigned firstIndex, unsigned primitives,
                             unsigned minVertex, unsigned vertexCount,
                             const float * world, const float * view)
{
    Impl & s = *impl_;
    s.error.clear();
    if (!s.enabled)
        return true;
    if (!frame.IsRecording() || !frame.ActiveCommandList() || !wvp || !normalWorld ||
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
    if (s.combiner.Requires_Bump_Shader())
        return s.Fail("W3D bump environment-map combiner requires a dedicated UV shader");
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
    constants.alphaCompare = static_cast<unsigned>(s.blend.alphaCompare);
    constants.material[0] = s.material.diffuse.r;
    constants.material[1] = s.material.diffuse.g;
    constants.material[2] = s.material.diffuse.b;
    constants.material[3] = s.material.diffuse.a;
    constants.textureFactor[0] = ((s.textureFactor >> 16) & 255u) / 255.f;
    constants.textureFactor[1] = ((s.textureFactor >> 8) & 255u) / 255.f;
    constants.textureFactor[2] = (s.textureFactor & 255u) / 255.f;
    constants.textureFactor[3] = ((s.textureFactor >> 24) & 255u) / 255.f;
    const float * materialAmbient = &s.material.ambient.r;
    const float * materialSpecular = &s.material.specular.r;
    std::memcpy(constants.materialSpecular, materialSpecular, sizeof(constants.materialSpecular));
    constants.specularProperties[0] = s.material.power;
    constants.specularProperties[1] = s.specularEnabled ? 1.f : 0.f;
    const float identity[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    std::memcpy(constants.world, world ? world : identity, sizeof(constants.world));
    std::memcpy(constants.view, view ? view : identity, sizeof(constants.view));
    const float * materialEmissive = &s.material.emissive.r;
    std::memcpy(constants.materialAmbient, materialAmbient, sizeof(constants.materialAmbient));
    std::memcpy(constants.materialEmissive, materialEmissive, sizeof(constants.materialEmissive));
    std::memcpy(constants.sceneAmbient, s.lighting.ambient, sizeof(constants.sceneAmbient));
    for (unsigned i = 0; i < 4; ++i)
    {
        std::memcpy(constants.lightDirection[i], s.lighting.lights[i].direction, sizeof(constants.lightDirection[i]));
        std::memcpy(constants.lightDiffuse[i], s.lighting.lights[i].diffuse, sizeof(constants.lightDiffuse[i]));
        constants.lightDirection[i][3] = static_cast<float>(s.lighting.lights[i].type);
        std::memcpy(constants.lightSpecular[i], s.lighting.lights[i].specular, sizeof(constants.lightSpecular[i]));
        std::memcpy(constants.lightPosition[i], s.lighting.lights[i].position, sizeof(constants.lightPosition[i]));
        std::memcpy(constants.lightSpotDirection[i], s.lighting.lights[i].spotDirection, sizeof(constants.lightSpotDirection[i]));
        std::memcpy(constants.lightAttenuation[i], s.lighting.lights[i].attenuation, sizeof(constants.lightAttenuation[i]));
    }
    constants.lightCount = s.lighting.lightCount;
    constants.lightingEnabled = s.vertexLighting ? 1u : 0u;
    constants.ambientSource = static_cast<unsigned>(s.colorSources.ambient);
    constants.diffuseSource = static_cast<unsigned>(s.colorSources.diffuse);
    constants.emissiveSource = static_cast<unsigned>(s.colorSources.emissive);

    // The native gameplay submission uses row-vectors: W * V * P.
    // Lighting needs the inverse-transpose of the world 3x3 to avoid
    // skewed normals on non-uniformly scaled W3D meshes.
    // The caller supplies the normal transform alongside its WVP.
    std::memcpy(constants.normalToWorld, normalWorld, sizeof(constants.normalToWorld));

    ID3D12Resource * upload = nullptr;
    unsigned uploadOffset = 0;
    if (!frame.ReserveTransientGeometry(static_cast<unsigned>(sizeof(constants)) + 255u,
                                        upload, uploadOffset) || !upload)
        return s.Fail(frame.LastError());
    const unsigned alignedOffset = (uploadOffset + 255u) & ~255u;
    void * mapped = nullptr;
    const D3D12_RANGE noRead = {0, 0};
    if (FAILED(upload->Map(0, &noRead, &mapped)) || !mapped)
        return s.Fail("Mapping native W3D per-draw lighting CBV failed");
    std::memcpy(static_cast<unsigned char *>(mapped) + alignedOffset, &constants, sizeof(constants));
    const D3D12_RANGE written = {alignedOffset, alignedOffset + sizeof(constants)};
    upload->Unmap(0, &written);
    if (!frame.RetainFrameResource(upload))
        return s.Fail(frame.LastError());

    auto * cmd = frame.ActiveCommandList();
    cmd->SetGraphicsRootSignature(s.root.Get());
    cmd->SetPipelineState(pipeline);
    cmd->SetGraphicsRootConstantBufferView(0, upload->GetGPUVirtualAddress() + alignedOffset);
    if (!stages.BindForDraw(frame, 1, 3))
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
