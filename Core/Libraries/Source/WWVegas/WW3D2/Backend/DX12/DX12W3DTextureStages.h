/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#pragma once

#include "../../Renderer.h"
#include <memory>

class DX12PresentationDevice;

// R05: selected DX12 backend's strongly referenced W3D texture stages and
// immutable, shader-visible descriptor tables. Call BindForDraw only after
// setting the native W3D root signature; root parameters firstRoot and
// firstRoot+1 must each expose a single pixel-shader SRV (t0, t1).
//
// W3D may bind up to eight legacy stages, but the currently ported unlit
// material combiner samples the first two. M01/B07 will call BindForDraw
// before authoritative W3D draw submissions; this interface never touches
// a D3D8 device or the legacy preview's private descriptor heap.
class DX12W3DTextureStages final
{
public:
    static constexpr unsigned StageCount = 8;
    DX12W3DTextureStages();
    ~DX12W3DTextureStages();
    DX12W3DTextureStages(const DX12W3DTextureStages &) = delete;
    DX12W3DTextureStages & operator=(const DX12W3DTextureStages &) = delete;

    // Stage changes own one texture reference; callers may immediately
    // release their own reference after binding. Null explicitly unbinds.
    bool Bind(unsigned stage, RenderTextureHandle texture);
    void Clear();
    RenderTextureHandle Bound(unsigned stage) const;

    // Per-stage samplers are immutable snapshots for recorded DX12 draws.
    // Native W3D uses the optional sampler descriptor table; the R05
    // preview remains compatible with its existing static samplers.
    bool ApplySampler(unsigned stage, const RenderSamplerDescription & sampler);
    bool SetFilter(unsigned stage, RenderSamplerFilterField field, RenderSamplerFilter value);
    bool SetAddress(unsigned stage, RenderSamplerAxis axis, RenderSamplerAddress value);
    bool SetMaxAnisotropy(unsigned stage, unsigned value);
    bool SetBorderColor(unsigned stage, unsigned packedARGB);

    // Called once after BeginFrame and on mid-frame Bind_Texture updates.
    // Loads pending CPU mip revisions into native GPU generations.
    bool PrepareBound(DX12PresentationDevice & frame);

    // Immutable per-draw snapshot, not a mutable descriptor overwritten in
    // place. The command list retains each heap until its GPU fence completes
    // even after Bind(null), Clear() or the last W3D handle release.
    bool BindForDraw(DX12PresentationDevice & frame, unsigned firstRoot = 1,
                     unsigned samplerRoot = ~0u);

    unsigned SnapshotCount() const;
    unsigned PageCount() const;
    const char * LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Exclusive DX12 Renderer stage cache, shared with future native W3D draws.
// DX8/preview builds do not link this symbol. The active pass must have set
// its matching root signature before calling BindForDraw.
DX12W3DTextureStages & DX12W3DRendererTextureStages();
