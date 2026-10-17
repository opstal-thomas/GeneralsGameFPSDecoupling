/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
// R07: backend-private offscreen color/depth attachments and W3D surface
// bridging. No D3D8 COM object or gameplay-visible DXGI types.
#pragma once

#include "../../Renderer.h"
#include <d3d12.h>
#include <memory>

class DX12PresentationDevice;

class DX12W3DRenderTargets final
{
public:
    DX12W3DRenderTargets();
    ~DX12W3DRenderTargets();
    DX12W3DRenderTargets(const DX12W3DRenderTargets &) = delete;
    DX12W3DRenderTargets & operator=(const DX12W3DRenderTargets &) = delete;

    RenderTexture2DHandle CreateOffscreenTexture(DX12PresentationDevice & frame,
                                                 const RenderSurfaceDescription & desc);
    RenderTexture2DHandle CreateDepthTexture(DX12PresentationDevice & frame,
                                             const RenderTexture2DDescription & desc);

    // Obtained surfaces own a reference to their R04 texture, not its native
    // COM resource. This lets surviving handles rehydrate after device reset.
    RenderSurfaceHandle AcquireTextureLevel(DX12PresentationDevice & frame,
                                            RenderTexture2DHandle texture, unsigned mip);
    RenderSurfaceHandle AcquirePrimaryColor(DX12PresentationDevice & frame);
    RenderSurfaceHandle AcquirePrimaryDepth(DX12PresentationDevice & frame);
    RenderSurfaceHandle AcquireCurrentColor(DX12PresentationDevice & frame);
    RenderSurfaceHandle AcquireCurrentDepth(DX12PresentationDevice & frame);

    // Optional 4x MSAA path for postprocess/shadows. The resolved single-sample
    // texture is borrowed; Retain_Texture if it needs to outlive the surface.
    // Unsupported sample/format combinations are rejected, never silently
    // downgraded. Default W3D offscreen RTTs remain one-sample.
    RenderSurfaceHandle CreateMSAAColor(DX12PresentationDevice & frame,
                                         unsigned width, unsigned height,
                                         unsigned format, unsigned sampleCount);
    RenderTexture2DHandle ResolvedTexture(RenderSurfaceHandle surface) const;

    bool Owns(RenderSurfaceHandle surface) const;
    void Retain(RenderSurfaceHandle surface);
    void Release(RenderSurfaceHandle surface);
    bool Describe(RenderSurfaceHandle surface, RenderSurfaceDescription & out) const;

    bool BeginFrame(DX12PresentationDevice & frame);
    bool Bind(DX12PresentationDevice & frame, RenderSurfaceHandle color,
              RenderSurfaceHandle depth);
    bool Clear(DX12PresentationDevice & frame, bool color, bool depth,
               const float rgba[4], float z, unsigned stencil);
    bool EndFrame(DX12PresentationDevice & frame);
    void Reset(); // discard current binding; caller has synchronized/ended frame

    bool IsPrimary() const;
    RenderTextureHandle TextureForSurface(RenderSurfaceHandle surface) const;
    RenderTextureHandle BoundColorTexture() const;
    RenderTextureHandle BoundDepthTexture() const;
    ID3D12Resource * BoundColorResource(DX12PresentationDevice & frame) const;
    ID3D12Resource * BoundDepthResource(DX12PresentationDevice & frame) const;
    unsigned BoundSampleCount() const;

    // R08: borrowed native color image for GPU copies/readback. MSAA reads
    // resolve to their R07 single-sample attachment, never map multisample
    // memory directly. Depth transfer is rejected until stencil layout parity.
    bool TransferResource(DX12PresentationDevice & frame, RenderSurfaceHandle surface,
                          bool destination, ID3D12Resource * & resource);
    bool TransitionTransfer(DX12PresentationDevice & frame, RenderSurfaceHandle surface,
                            D3D12_RESOURCE_STATES desired);
    // FlushAndContinue clears OM binding. Rebind the *current* R07 color/depth
    // descriptors, preserving the active offscreen surface identities.
    bool RebindAfterFlush(DX12PresentationDevice & frame);
    const char * LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
