/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#include "DX12W3DVertexLayouts.h"

namespace
{
    // Numeric D3DFVF values from legacy vertex buffers (not D3D8 types).
    constexpr unsigned PositionMask = 0x400eu;
    constexpr unsigned XYZ = 0x0002u;
    constexpr unsigned XYZRHW = 0x0004u;
    constexpr unsigned XYZB1 = 0x0006u;
    constexpr unsigned XYZB5 = 0x000eu;
    constexpr unsigned XYZW = 0x4002u;
    constexpr unsigned Normal = 0x0010u;
    constexpr unsigned PointSize = 0x0020u;
    constexpr unsigned Diffuse = 0x0040u;
    constexpr unsigned Specular = 0x0080u;
    constexpr unsigned TexCountMask = 0x0f00u;
    constexpr unsigned LastBetaUByte4 = 0x1000u;
    constexpr unsigned LastBetaColor = 0x8000u;
    constexpr unsigned Allowed = PositionMask | Normal | PointSize | Diffuse |
        Specular | TexCountMask | LastBetaUByte4 | LastBetaColor | 0xffff0000u;

    bool Fail(std::string * error, const char * text)
    {
        if (error)
            *error = text;
        return false;
    }

    void Add(DX12W3DVertexLayout & layout, const char * semantic, unsigned index,
             DXGI_FORMAT format, unsigned sizeBytes)
    {
        layout.elements[layout.elementCount++] = {
            semantic, index, format, 0, layout.stride,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        };
        layout.stride += sizeBytes;
    }

    DXGI_FORMAT FloatFormat(unsigned floats)
    {
        switch (floats)
        {
        case 1: return DXGI_FORMAT_R32_FLOAT;
        case 2: return DXGI_FORMAT_R32G32_FLOAT;
        case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    }
}

bool DX12W3DVertexLayouts::DecodeLegacyFVF(
    unsigned fvf, DX12W3DVertexLayout & out, std::string * error)
{
    out = {};
    if (error)
        error->clear();
    if ((fvf & ~Allowed) != 0)
        return Fail(error, "Unknown or reserved W3D FVF bits");

    const unsigned position = fvf & PositionMask;
    if (position != XYZ && position != XYZRHW && position != XYZW &&
        (position < XYZB1 || position > XYZB5 || (position & 1u)))
        return Fail(error, "Unsupported W3D FVF position encoding");

    const unsigned texCount = (fvf & TexCountMask) >> 8;
    if (texCount > 8)
        return Fail(error, "W3D FVF has more than eight texture-coordinate sets");

    const unsigned betaType = fvf & (LastBetaUByte4 | LastBetaColor);
    if (betaType == (LastBetaUByte4 | LastBetaColor))
        return Fail(error, "Contradictory W3D blend-index encodings");

    const unsigned blendCount = position >= XYZB1 && position <= XYZB5
                                    ? (position - XYZRHW) / 2 : 0;
    if (betaType && blendCount == 0)
        return Fail(error, "W3D blend indices require an XYZBn position");
    if (position == XYZRHW && (fvf & Normal))
        return Fail(error, "Transformed XYZRHW vertices cannot have normals");

    // Unused texture-coordinate size flags cannot alias a later input.
    for (unsigned set = texCount; set < 8; ++set)
        if (((fvf >> (16 + set * 2)) & 3u) != 0)
            return Fail(error, "FVF has size flags for an undeclared UV set");

    DX12W3DVertexLayout layout;
    layout.legacyFVF = fvf;
    layout.transformed = position == XYZRHW;
    Add(layout, "POSITION", 0,
        position == XYZRHW || position == XYZW
            ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT,
        position == XYZRHW || position == XYZW ? 16 : 12);

    // LASTBETA occupies the final float's four bytes with bone indices.
    const unsigned weights = blendCount - (betaType ? 1u : 0u);
    if (weights)
    {
        const unsigned first = weights > 4 ? 4 : weights;
        Add(layout, "BLENDWEIGHT", 0, FloatFormat(first), first * 4);
        if (weights > 4)
            Add(layout, "BLENDWEIGHT", 1, DXGI_FORMAT_R32_FLOAT, 4);
    }
    if (betaType)
        Add(layout, "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 4);

    if (fvf & Normal)
        Add(layout, "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12);
    if (fvf & PointSize)
        Add(layout, "PSIZE", 0, DXGI_FORMAT_R32_FLOAT, 4);

    // D3DCOLOR is stored in BGRA byte order. IA sees RGBA byte channels;
    // consumers must swizzle .bgra in the shader (no hidden CPU conversion).
    if (fvf & Diffuse)
        Add(layout, "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
    if (fvf & Specular)
        Add(layout, "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
    layout.packedColorNeedsBGRASwizzle = (fvf & (Diffuse | Specular)) != 0;

    for (unsigned set = 0; set < texCount; ++set)
    {
        const unsigned dimensionCode = (fvf >> (16 + set * 2)) & 3u;
        // D3DFVF_TEXCOORDSIZE2 == 0, SIZE3 == 1, SIZE4 == 2, SIZE1 == 3.
        const unsigned dimensions = dimensionCode == 3 ? 1 : dimensionCode + 2;
        Add(layout, "TEXCOORD", set, FloatFormat(dimensions), dimensions * 4);
    }
    out = layout;
    return true;
}
