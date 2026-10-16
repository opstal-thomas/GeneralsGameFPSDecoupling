/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#pragma once

// M01: typed WW3D sorting-state ownership lives with the original CPU mesh
// categories. The selected D3D12 Renderer implementation receives only
// backend-neutral buffer slices and matrix/light descriptions.
struct RenderDynamicVertexBinding;
struct RenderDynamicIndexBinding;

#if defined(W3D_RENDER_BACKEND_DX12)
void DX12W3DSortingTrackVertexBinding(const RenderDynamicVertexBinding & binding);
void DX12W3DSortingTrackIndexBinding(const RenderDynamicIndexBinding & binding,
                                    unsigned short baseVertexIndex);
unsigned DX12W3DSortingCurrentBaseVertexIndex();
#endif
