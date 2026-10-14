// Minimal W3D unlit Direct3D 12 shader proof of concept.
// Not wired into the game renderer yet. This intentionally supports only
// position, diffuse color and two UV sets; vertex lighting, fog and bump
// mapping need their own permutations before visual parity is possible.
// The shader is compiled for SM5.1 DXBC, which Direct3D 12 can consume.
#include "W3DLegacyTextureCombiner.hlsli"

cbuffer W3DPerDraw : register(b0)
{
    row_major float4x4 W3D_WorldViewProjection;
    float4 W3D_TextureFactor;
    uint W3D_CombinerKey;
    uint W3D_AlphaTestEnabled;
    uint W3D_AlphaReference;
    uint W3D_AlphaCompare;
};

Texture2D W3D_Texture0 : register(t0);
Texture2D W3D_Texture1 : register(t1);
SamplerState W3D_Sampler0 : register(s0);
SamplerState W3D_Sampler1 : register(s1);

struct W3DVertexInput
{
    float3 position : POSITION;
    float4 diffuse : COLOR0;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
};

struct W3DVertexOutput
{
    float4 position : SV_POSITION;
    float4 diffuse : COLOR0;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
};

W3DVertexOutput W3D_VS(W3DVertexInput input)
{
    W3DVertexOutput output;
    output.position = mul(float4(input.position, 1.0f), W3D_WorldViewProjection);
    output.diffuse = input.diffuse;
    output.uv0 = input.uv0;
    output.uv1 = input.uv1;
    return output;
}

bool W3DAlphaComparison(float alpha, uint reference, uint compare)
{
    // RenderCompareFunc order from Renderer.h. Reference is the original
    // eight-bit DX8 alpha-test threshold; convert it to normalized alpha.
    const float r = float(reference & 0xffu) / 255.0f;
    if (compare == 0u) return false;            // never
    if (compare == 1u) return alpha < r;        // less
    if (compare == 2u) return alpha == r;       // equal
    if (compare == 3u) return alpha <= r;       // less or equal
    if (compare == 4u) return alpha > r;        // greater
    if (compare == 5u) return alpha != r;       // not equal
    if (compare == 6u) return alpha >= r;       // greater or equal
    return true;                                // always
}

float4 W3D_PS(W3DVertexOutput input) : SV_TARGET0
{
    const uint color0 = W3DGetChannel(W3D_CombinerKey, 0u, false);
    const uint alpha0 = W3DGetChannel(W3D_CombinerKey, 0u, true);
    const uint color1 = W3DGetChannel(W3D_CombinerKey, 1u, false);
    const uint alpha1 = W3DGetChannel(W3D_CombinerKey, 1u, true);

    // Bump-environment map operators modify the next stage's UVs. They
    // require a separate shader path; make unsupported permutations obvious
    // rather than silently displaying a wrong material.
    if (W3DRequiresBumpShader(W3DChannelOperation(color0)) ||
        W3DRequiresBumpShader(W3DChannelOperation(alpha0)) ||
        W3DRequiresBumpShader(W3DChannelOperation(color1)) ||
        W3DRequiresBumpShader(W3DChannelOperation(alpha1)))
    {
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }

    float4 current = input.diffuse;
    if (W3DChannelOperation(color0) != W3D_OP_DISABLE)
    {
        const float4 texel0 = W3D_Texture0.Sample(W3D_Sampler0, input.uv0);
        current = W3DApplyTextureStage(
            color0, alpha0, current, texel0, input.diffuse, W3D_TextureFactor);

        // A disabled COLOROP ends the fixed-function texture cascade.
        if (W3DChannelOperation(color1) != W3D_OP_DISABLE)
        {
            const float4 texel1 = W3D_Texture1.Sample(W3D_Sampler1, input.uv1);
            current = W3DApplyTextureStage(
                color1, alpha1, current, texel1, input.diffuse, W3D_TextureFactor);
        }
    }

    if (W3D_AlphaTestEnabled != 0u)
    {
        const bool alphaPasses =
            W3DAlphaComparison(current.a, W3D_AlphaReference, W3D_AlphaCompare);
        clip(alphaPasses ? 1.0f : -1.0f);
    }
    return current;
}
