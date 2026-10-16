/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/

// R01: backend-private implementation of Renderer.h's opaque geometry handles.
// No D3D8 interfaces, device globals or gameplay-owned graphics resources.
#pragma once

#include "../../Renderer.h"
#include "DX12W3DVertexLayouts.h"
#include <d3d12.h>

class DX12PresentationDevice;

namespace DX12W3DGeometryBuffers
{
    // Persistent geometry is created in the DEFAULT heap at first binding;
    // CPU writes are staged and copied on the active DX12 command list.
    // R02 dynamic geometry is snapshotted to immutable slices of a shared
    // UPLOAD page. The owning presentation frame fence controls page reuse.
    RenderVertexBufferHandle CreateVertex(ID3D12Device * device,
                                          const RenderBufferDescription & description);
    RenderIndexBufferHandle CreateIndex(ID3D12Device * device,
                                        const RenderBufferDescription & description);
    void ReleaseVertex(RenderVertexBufferHandle handle);
    void ReleaseIndex(RenderIndexBufferHandle handle);
    void * MapVertex(RenderVertexBufferHandle handle, unsigned offsetBytes,
                     unsigned sizeBytes, unsigned mapFlags);
    void * MapIndex(RenderIndexBufferHandle handle, unsigned offsetBytes,
                    unsigned sizeBytes, unsigned mapFlags);
    bool UnmapVertex(RenderVertexBufferHandle handle);
    bool UnmapIndex(RenderIndexBufferHandle handle);

    bool BindVertex(DX12PresentationDevice & device,
                    RenderVertexBufferHandle handle, unsigned stream,
                    unsigned stride, unsigned firstVertex = 0,
                    unsigned vertexCount = 0);
    bool BindIndex(DX12PresentationDevice & device,
                   RenderIndexBufferHandle handle, unsigned baseVertexIndex,
                   unsigned firstIndex = 0, unsigned indexCount = 0);

    // For native WARP tests and the later authoritative draw path only:
    // callers do not own these GPU resources, and must not release them.
    ID3D12Resource * NativeVertexResource(RenderVertexBufferHandle handle);
    ID3D12Resource * NativeIndexResource(RenderIndexBufferHandle handle);
    // Backend-private byte offsets for validating pooled upload snapshots.
    unsigned NativeVertexOffset(RenderVertexBufferHandle handle);
    unsigned NativeIndexOffset(RenderIndexBufferHandle handle);
    const DX12W3DVertexLayout * NativeVertexLayout(RenderVertexBufferHandle handle);
    unsigned NativeIndexElementBytes(RenderIndexBufferHandle handle);
    unsigned VertexByteSize(RenderVertexBufferHandle handle);
    unsigned IndexByteSize(RenderIndexBufferHandle handle);
    bool IsVertexMapped(RenderVertexBufferHandle handle);
    bool IsIndexMapped(RenderIndexBufferHandle handle);
    const char * LastError();
}
