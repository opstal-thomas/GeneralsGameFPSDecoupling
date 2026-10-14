// Command & Conquer Generals / Zero Hour W3D renderer modernization.
// Reusable Direct3D 12 HLSL reference for the original fixed-function
// texture combiner. This file is not yet part of the running renderer.
//
// Numeric IDs are the RenderTextureOperation/Argument enum ordinals in
// WW3D2/Renderer.h; RendererBackendTest locks the corresponding C++ layout.
// The DX12 shader compiler will specialize these values when building PSOs.
#ifndef W3D_DX12_LEGACY_TEXTURE_COMBINER_INCLUDED
#define W3D_DX12_LEGACY_TEXTURE_COMBINER_INCLUDED

static const uint W3D_OP_DISABLE = 0u;
static const uint W3D_OP_SELECT_ARG1 = 1u;
static const uint W3D_OP_SELECT_ARG2 = 2u;
static const uint W3D_OP_MODULATE = 3u;
static const uint W3D_OP_MODULATE2X = 4u;
static const uint W3D_OP_ADD = 5u;
static const uint W3D_OP_ADD_SMOOTH = 6u;
static const uint W3D_OP_SUBTRACT = 7u;
static const uint W3D_OP_BLEND_TEXTURE_ALPHA = 8u;
static const uint W3D_OP_BLEND_CURRENT_ALPHA = 9u;
static const uint W3D_OP_ADD_SIGNED = 10u;
static const uint W3D_OP_ADD_SIGNED2X = 11u;
static const uint W3D_OP_MODULATE_ALPHA_ADD_COLOR = 12u;
static const uint W3D_OP_BUMP_ENV_MAP = 13u;
static const uint W3D_OP_BUMP_ENV_MAP_LUMINANCE = 14u;

static const uint W3D_ARG_CURRENT = 0u;
static const uint W3D_ARG_TEXTURE = 1u;
static const uint W3D_ARG_DIFFUSE = 2u;
static const uint W3D_ARG_TEXTURE_FACTOR = 3u;

// The color and alpha channels each have one byte of a 32-bit
// RenderTextureCombinerKey. No platform-specific texture-stage constants
// or GPU resource pointers are present in that encoding.
uint W3DChannelOperation(uint channelByte) { return channelByte & 0x0fu; }
uint W3DChannelArg1(uint channelByte) { return (channelByte >> 4) & 0x03u; }
uint W3DChannelArg2(uint channelByte) { return (channelByte >> 6) & 0x03u; }
uint W3DGetChannel(uint packedKey, uint stage, bool alphaChannel)
{
    uint shift = stage * 16u + (alphaChannel ? 8u : 0u);
    return (packedKey >> shift) & 0xffu;
}

bool W3DRequiresBumpShader(uint op)
{
    return op == W3D_OP_BUMP_ENV_MAP || op == W3D_OP_BUMP_ENV_MAP_LUMINANCE;
}

float4 W3DGetTextureArgument(
    uint argument, float4 previous, float4 texel,
    float4 vertexDiffuse, float4 textureFactor)
{
    if (argument == W3D_ARG_CURRENT) return previous;
    if (argument == W3D_ARG_TEXTURE) return texel;
    if (argument == W3D_ARG_DIFFUSE) return vertexDiffuse;
    if (argument == W3D_ARG_TEXTURE_FACTOR) return textureFactor;
    return previous;
}

// A standard W3D combinational stage. The bump-map operators perturb
// the next stage's texture coordinates and are NOT supported here:
// select a dedicated shader variant when W3DRequiresBumpShader returns true.
float4 W3DCombineStandard(
    uint operation, float4 arg1, float4 arg2,
    float4 previous, float4 texel)
{
    if (operation == W3D_OP_SELECT_ARG1) return arg1;
    if (operation == W3D_OP_SELECT_ARG2) return arg2;
    if (operation == W3D_OP_MODULATE) return saturate(arg1 * arg2);
    if (operation == W3D_OP_MODULATE2X) return saturate(2.0f * arg1 * arg2);
    if (operation == W3D_OP_ADD) return saturate(arg1 + arg2);
    if (operation == W3D_OP_ADD_SMOOTH) return saturate(arg1 + arg2 - arg1 * arg2);
    if (operation == W3D_OP_SUBTRACT) return saturate(arg1 - arg2);
    if (operation == W3D_OP_BLEND_TEXTURE_ALPHA)
        return saturate(lerp(arg2, arg1, texel.a));
    if (operation == W3D_OP_BLEND_CURRENT_ALPHA)
        return saturate(lerp(arg2, arg1, previous.a));
    if (operation == W3D_OP_ADD_SIGNED)
        return saturate(arg1 + arg2 - 0.5f);
    if (operation == W3D_OP_ADD_SIGNED2X)
        return saturate(2.0f * (arg1 + arg2 - 0.5f));
    if (operation == W3D_OP_MODULATE_ALPHA_ADD_COLOR)
        return saturate(arg1 + arg1.a * arg2);

    // The caller must handle DISABLE and bump operators separately.
    return previous;
}

// Color and alpha both read the same *previous* stage value, not the
// partially updated output of this stage. COLOROP=DISABLE must stop
// subsequent stages in the calling pixel-shader permutation.
float4 W3DApplyTextureStage(
    uint colorChannel, uint alphaChannel,
    float4 previous, float4 texel,
    float4 vertexDiffuse, float4 textureFactor)
{
    const uint colorOp = W3DChannelOperation(colorChannel);
    if (colorOp == W3D_OP_DISABLE) return previous;

    const float4 colorA = W3DGetTextureArgument(
        W3DChannelArg1(colorChannel), previous, texel, vertexDiffuse, textureFactor);
    const float4 colorB = W3DGetTextureArgument(
        W3DChannelArg2(colorChannel), previous, texel, vertexDiffuse, textureFactor);
    const float4 resultColor = W3DCombineStandard(
        colorOp, colorA, colorB, previous, texel);

    const uint alphaOp = W3DChannelOperation(alphaChannel);
    if (alphaOp == W3D_OP_DISABLE)
        return float4(resultColor.rgb, previous.a);

    const float4 alphaA = W3DGetTextureArgument(
        W3DChannelArg1(alphaChannel), previous, texel, vertexDiffuse, textureFactor);
    const float4 alphaB = W3DGetTextureArgument(
        W3DChannelArg2(alphaChannel), previous, texel, vertexDiffuse, textureFactor);
    const float4 resultAlpha = W3DCombineStandard(
        alphaOp, alphaA, alphaB, previous, texel);
    return float4(resultColor.rgb, resultAlpha.a);
}

#endif // W3D_DX12_LEGACY_TEXTURE_COMBINER_INCLUDED
