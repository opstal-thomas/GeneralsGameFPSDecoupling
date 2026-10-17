/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#pragma once
// R08: CPU<->GPU image copies and synchronous screenshot readback. The game
// observes only Renderer.h's opaque surface/texture handles.
#include "../../Renderer.h"
#include <memory>

class DX12PresentationDevice;
class DX12W3DRenderTargets;

class DX12W3DSurfaceTransfers final
{
public:
    DX12W3DSurfaceTransfers();
    ~DX12W3DSurfaceTransfers();
    DX12W3DSurfaceTransfers(const DX12W3DSurfaceTransfers &) = delete;
    DX12W3DSurfaceTransfers & operator=(const DX12W3DSurfaceTransfers &) = delete;

    void Reset();
    // Returns tightly packed, legacy A8R8G8B8/BGRA CPU bytes.
    unsigned ReadRegion(DX12PresentationDevice & frame,
                        DX12W3DRenderTargets & targets,
                        RenderSurfaceHandle surface, unsigned char * destination,
                        unsigned capacity, const RenderSurfaceRect & region);
    bool Lock(DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
              RenderSurfaceHandle surface, RenderSurfaceMap & mapped,
              const RenderSurfaceRect * region, bool readOnly);
    bool Unlock(DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
                RenderSurfaceHandle surface);
    bool Copy(DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
              RenderSurfaceHandle source, const RenderSurfaceRect * sourceRect,
              RenderSurfaceHandle destination, const RenderSurfacePoint * destinationPoint);
    bool Blit(DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
              RenderSurfaceHandle source, const RenderSurfaceRect & sourceRect,
              RenderSurfaceHandle destination, const RenderSurfaceRect & destinationRect,
              RenderSurfaceBlitFilter filter);
    RenderTexture2DHandle ToTexture(DX12PresentationDevice & frame,
                                    DX12W3DRenderTargets & targets,
                                    RenderSurfaceHandle source, unsigned mips);
    const char * LastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
