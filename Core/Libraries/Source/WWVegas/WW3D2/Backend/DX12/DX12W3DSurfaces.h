/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
// R04: backend-private CPU image surface, no IDirect3DSurface8 / D3DX8.
#pragma once
#include "../../Renderer.h"
#include <d3d12.h>

namespace DX12W3DSurfaces
{
    RenderSurfaceHandle Create(const RenderSurfaceDescription & description);
    void Retain(RenderSurfaceHandle handle);
    void Release(RenderSurfaceHandle handle);
    bool Describe(RenderSurfaceHandle handle, RenderSurfaceDescription & output);
    bool Lock(RenderSurfaceHandle handle, RenderSurfaceMap & output,
              const RenderSurfaceRect * region = nullptr, bool readOnly = false);
    bool Unlock(RenderSurfaceHandle handle);
    bool Copy(RenderSurfaceHandle source, const RenderSurfaceRect * rect,
              RenderSurfaceHandle destination, const RenderSurfacePoint * position);
    bool Blit(RenderSurfaceHandle source, const RenderSurfaceRect & sourceRect,
              RenderSurfaceHandle destination, const RenderSurfaceRect & destinationRect,
              RenderSurfaceBlitFilter filter);
    RenderTexture2DHandle ToTexture(ID3D12Device * device, RenderSurfaceHandle surface,
                                    unsigned mipLevels);
    // Used by DX12 MissingTexture::_Create_Missing_Surface and image loaders.
    unsigned RowPitch(RenderSurfaceHandle handle);
    const char * LastError();
}
