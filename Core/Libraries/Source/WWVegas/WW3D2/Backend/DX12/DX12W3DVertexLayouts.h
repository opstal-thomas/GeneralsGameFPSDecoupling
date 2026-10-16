/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/

// DX12-R03: interpret W3D/D3D8 fixed-function FVF numbers without
// depending on D3D8 headers or on the preview's canonicalized vertex struct.
#pragma once

#include <d3d12.h>
#include <array>
#include <string>

struct DX12W3DVertexLayout
{
    // 1 position + 2 blend-weight slots + 1 blend-index slot + 1 normal
    // + 1 point size + 2 packed colors + up to 8 texture coordinate sets.
    std::array<D3D12_INPUT_ELEMENT_DESC, 16> elements = {};
    unsigned elementCount = 0;
    unsigned stride = 0;
    unsigned legacyFVF = 0;
    bool transformed = false;
    bool packedColorNeedsBGRASwizzle = false;

    D3D12_INPUT_LAYOUT_DESC NativeInputLayout() const
    {
        return {elements.data(), static_cast<UINT>(elementCount)};
    }
};

namespace DX12W3DVertexLayouts
{
    // D3DFVF numerical values are part of the legacy asset format, but their
    // decoding does not link D3D8 or D3DX8. Reject unknown/contradictory bits.
    bool DecodeLegacyFVF(unsigned fvf, DX12W3DVertexLayout & out,
                         std::string * error = nullptr);
}
