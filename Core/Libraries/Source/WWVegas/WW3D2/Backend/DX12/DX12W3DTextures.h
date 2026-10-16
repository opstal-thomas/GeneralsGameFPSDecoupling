/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#pragma once

#include "../../Renderer.h"
#include <d3d12.h>
#include <cstddef>
#include <cstdint>

class DX12PresentationDevice;

namespace DX12W3DTextures
{
    // Native D3D12 textures with persistent CPU mip shadows. W3D's existing
    // DDS/TGA/thumbnail decoders write into Map* and Unmap*; GPU upload is
    // deferred until an active native command list can record a copy.
    RenderTexture2DHandle Create2D(ID3D12Device * device,
                                   const RenderTexture2DDescription & description);
    RenderTextureCubeHandle CreateCube(ID3D12Device * device,
                                       const RenderTextureCubeDescription & description);
    RenderTextureVolumeHandle CreateVolume(ID3D12Device * device,
                                           const RenderTextureVolumeDescription & description);

    unsigned MipCount(RenderTextureHandle handle);
    unsigned MemoryUsage(RenderTextureHandle handle);
    bool Describe(RenderTextureHandle handle, unsigned mip,
                  RenderTextureLevelDescription & output);
    bool Map2D(RenderTexture2DHandle handle, unsigned mip, RenderSurfaceMap & output);
    bool Unmap2D(RenderTexture2DHandle handle, unsigned mip);
    bool MapCube(RenderTextureCubeHandle handle, unsigned face, unsigned mip,
                 RenderSurfaceMap & output);
    bool UnmapCube(RenderTextureCubeHandle handle, unsigned face, unsigned mip);
    bool MapVolume(RenderTextureVolumeHandle handle, unsigned mip, RenderVolumeMap & output);
    bool UnmapVolume(RenderTextureVolumeHandle handle, unsigned mip);
    bool Update(RenderTextureHandle source, RenderTextureHandle target);

    // Does not expose a DX8 pointer. Binding records COPY/RESOURCE_BARRIER
    // operations if a frame is open; R05 owns the definitive SRV heap/stage
    // descriptor cache used by authoritative W3D draw submissions.
    bool Prepare(DX12PresentationDevice & frame, RenderTextureHandle texture);
    ID3D12Resource * NativeResource(RenderTextureHandle texture);
    DXGI_FORMAT NativeFormat(RenderTextureHandle texture);

    // The high-level W3D loader holds texture handles across async tasks.
    // References may change on the loader thread. Any bound native GPU
    // generations stay alive until the presentation queue's frame fence.
    void Retain(RenderTextureHandle handle);
    void Release(RenderTextureHandle handle);
    unsigned GetPriority(RenderTextureHandle handle);
    unsigned SetPriority(RenderTextureHandle handle, unsigned priority);

    // Palette conversion for legacy indexed input; packed 0xAARRGGBB.
    // Existing W3D BitmapHandlerClass already expands indexed TGA assets.
    bool ConvertPaletteToBGRA8(const std::uint8_t * source,
                               unsigned width, unsigned height, unsigned sourceRowPitch,
                               bool alphaIndexed, const std::uint32_t * palette,
                               std::uint8_t * destination, unsigned targetRowPitch);

    // CPU-only normalization also used by SurfaceClass and missing-texture
    // fallback before GPU residency; no D3DX or DX8 surface is required.
    bool ConvertRowToBGRA8(unsigned w3dFormat,
                           const std::uint8_t * src, unsigned pixels,
                           std::uint8_t * destination);

    bool SupportsFormat(unsigned w3dFormat);
    const char * LastError();
}
