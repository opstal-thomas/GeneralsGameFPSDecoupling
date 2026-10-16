/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#pragma once

// M01/B07: *authoritative* indexed draws using original W3D R02/R03 geometry
// bindings on the game's primary presentation. No DX8 mirror, DX8 interface,
// imported HLSL file or preview-only reconstructed vertex data.
#include "DX12W3DVertexLayouts.h"
#include "../../Renderer.h"
#include <memory>

class DX12PresentationDevice;
class DX12W3DTextureStages;
class DX12W3DRenderTargets;

class DX12W3DNativeDraw final
{
public:
    DX12W3DNativeDraw();
    ~DX12W3DNativeDraw();
    DX12W3DNativeDraw(const DX12W3DNativeDraw &) = delete;
    DX12W3DNativeDraw & operator=(const DX12W3DNativeDraw &) = delete;

    void Reset();
    void BeginFrame(); // drop stale IA bindings, preserve material state
    void BindVertex(const DX12W3DVertexLayout * layout, unsigned count);
    void BindIndex(unsigned count);
    void SetBlend(const RenderBlendState & blend);
    void SetSourceBlend(RenderBlendFactor factor);
    void SetDestinationBlend(RenderBlendFactor factor);
    void SetBlendOperation(RenderBlendOperation operation);
    void SetBlendingEnabled(bool enabled);
    void SetColorWriteMask(bool rgbEnabled, bool alphaEnabled);
    void SetMaterial(const RenderVertexMaterialDescription & material);
    void SetDepthCompare(RenderCompareFunc compare);
    void SetDepthWrite(bool enabled);
    void SetCull(RenderCullMode cull);
    void SetAlphaTest(bool enabled);
    void SetDrawEnabled(bool enabled);
    bool IsDrawEnabled() const;
    unsigned PipelineCount() const;
    unsigned DrawCount() const;
    const char * LastError() const;

    bool Draw(DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
              DX12W3DTextureStages & stages, const float wvp[16],
              bool strip, unsigned firstIndex, unsigned primitiveCount,
              unsigned minVertex, unsigned vertexCount);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
