/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#include "DX12W3DGeometryBuffers.h"
#include "DX12PresentationDevice.h"

#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    thread_local std::string lastError;

    bool Fail(const char * text)
    {
        lastError = text;
        return false;
    }

    D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES properties = {};
        properties.Type = type;
        properties.CreationNodeMask = properties.VisibleNodeMask = 1;
        return properties;
    }

    D3D12_RESOURCE_DESC BufferDesc(unsigned count)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = count;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.SampleDesc.Count = 1;
        return desc;
    }

    bool MakeBuffer(ID3D12Device * device, unsigned byteSize,
                    D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initial,
                    ComPtr<ID3D12Resource> & resource)
    {
        const D3D12_HEAP_PROPERTIES heap = Heap(heapType);
        const D3D12_RESOURCE_DESC desc = BufferDesc(byteSize);
        return SUCCEEDED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, initial, nullptr,
            IID_PPV_ARGS(&resource)));
    }

}

// One opaque handle owns W3D byte storage, native resource generations, and
// the original device identity. No D3D8 COM resources or global device state.
struct DX12W3DBufferState
{
    ComPtr<ID3D12Device> owner;
    std::vector<unsigned char> shadow;
    ComPtr<ID3D12Resource> native;
    D3D12_RESOURCE_STATES residentState = D3D12_RESOURCE_STATE_COMMON;
    unsigned nativeOffset = 0;
    unsigned format = 0;
    DX12W3DVertexLayout vertexLayout;
    unsigned indexElementBytes = 2;
    bool dynamic = false;
    bool mapped = false;
    bool readOnly = false;
    bool dirty = true;
    unsigned long long preparedRecording = 0;

    explicit DX12W3DBufferState(ID3D12Device * device,
                                const RenderBufferDescription & desc)
        : owner(device), shadow(desc.byteSize, 0),
          format(desc.legacyVertexFormat),
          indexElementBytes(static_cast<unsigned>(desc.indexFormat)),
          dynamic(desc.Is_Dynamic()) {}

    bool Map(unsigned offset, unsigned size, unsigned flags, void * & result)
    {
        result = nullptr;
        const unsigned known = RENDER_MAP_DISCARD | RENDER_MAP_NO_OVERWRITE |
                               RENDER_MAP_NO_SYSTEM_LOCK | RENDER_MAP_READ_ONLY;
        if (mapped || (flags & ~known) || offset >= shadow.size() ||
            (size && size > shadow.size() - offset))
            return Fail("Invalid, nested, or out-of-range DX12 geometry map");

        readOnly = (flags & RENDER_MAP_READ_ONLY) != 0;
        const bool discard = (flags & RENDER_MAP_DISCARD) != 0;
        const bool noOverwrite = (flags & RENDER_MAP_NO_OVERWRITE) != 0;
        if ((discard && (!dynamic || noOverwrite || readOnly)) ||
            (noOverwrite && (!dynamic || readOnly)))
            return Fail("Unsupported static or contradictory geometry map flags");

        // DISCARD makes previous contents undefined. Zeroing the shadow
        // establishes deterministic bytes for partial writes. An entirely
        // new GPU allocation is made when the dirty buffer is next bound.
        if (discard)
            std::fill(shadow.begin(), shadow.end(), 0);
        mapped = true;
        result = shadow.data() + offset;
        return true;
    }

    bool Unmap()
    {
        if (!mapped)
            return Fail("Unmap without an active DX12 geometry map");
        mapped = false;
        if (!readOnly)
            dirty = true;
        readOnly = false;
        return true;
    }

    bool Prepare(DX12PresentationDevice & device, bool indices)
    {
        if (mapped || !device.ActiveCommandList() || !device.NativeDevice())
            return Fail("Geometry must be unmapped and bound during a native DX12 scene");
        if (device.NativeDevice() != owner.Get())
            return Fail("Geometry belongs to another D3D12 device generation");

        const auto finalState = indices ? D3D12_RESOURCE_STATE_INDEX_BUFFER
                                        : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

        // Transient pages are owned by the presentation and may be recycled
        // after the last using fence. Even an unchanged dynamic W3D handle
        // must resnapshot its CPU shadow when rebound in a later recording.
        // Static DEFAULT-heap resources remain persistent between frames.
        if (!native || dirty || (dynamic && preparedRecording != device.RecordingSerial()))
        {
            ComPtr<ID3D12Resource> newNative;
            if (dynamic)
            {
                // DISCARD and NO_OVERWRITE each receive an immutable slice.
                ID3D12Resource * borrowed = nullptr;
                if (!device.ReserveTransientGeometry(
                        static_cast<unsigned>(shadow.size()), borrowed, nativeOffset))
                    return Fail(device.LastError());
                newNative = borrowed; // retain independently of the pool
                void * mappedBytes = nullptr;
                const D3D12_RANGE noRead = {0, 0};
                if (FAILED(newNative->Map(0, &noRead, &mappedBytes)) || !mappedBytes)
                    return Fail("Mapping transient upload page failed");
                std::memcpy(static_cast<unsigned char *>(mappedBytes) + nativeOffset,
                            shadow.data(), shadow.size());
                const D3D12_RANGE written = {nativeOffset,
                                              nativeOffset + shadow.size()};
                newNative->Unmap(0, &written);
                residentState = D3D12_RESOURCE_STATE_GENERIC_READ;
            }
            else
            {
                nativeOffset = 0;
                // True GPU-local static geometry, not a second upload-heap
                // resource misrepresented as a static vertex/index buffer.
                if (!MakeBuffer(owner.Get(), static_cast<unsigned>(shadow.size()),
                                D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                                newNative))
                    return Fail("Creating DEFAULT heap geometry resource failed");
                ComPtr<ID3D12Resource> staging;
                if (!MakeBuffer(owner.Get(), static_cast<unsigned>(shadow.size()),
                                D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                                staging))
                    return Fail("Creating static geometry upload staging failed");
                void * mappedBytes = nullptr;
                const D3D12_RANGE noRead = {0, 0};
                if (FAILED(staging->Map(0, &noRead, &mappedBytes)) || !mappedBytes)
                    return Fail("Mapping static geometry staging failed");
                std::memcpy(mappedBytes, shadow.data(), shadow.size());
                const D3D12_RANGE written = {0, shadow.size()};
                staging->Unmap(0, &written);

                if (!device.RegisterFrameResource(newNative.Get(),
                                                  D3D12_RESOURCE_STATE_COPY_DEST) ||
                    !device.RetainFrameResource(newNative.Get()) ||
                    !device.RetainFrameResource(staging.Get()))
                    return Fail(device.LastError());

                device.ActiveCommandList()->CopyBufferRegion(
                    newNative.Get(), 0, staging.Get(), 0, shadow.size());
                if (!device.TransitionFrameResource(newNative.Get(), finalState))
                    return Fail(device.LastError());
                residentState = finalState;
            }
            // Replacing a buffer preserves the previous version for any
            // already-recorded draws, pinned by RetainFrameResource().
            native = std::move(newNative);
        }

        D3D12_RESOURCE_STATES tracked = D3D12_RESOURCE_STATE_COMMON;
        if (!device.GetFrameResourceState(native.Get(), tracked))
        {
            if (!device.RegisterFrameResource(native.Get(), residentState))
                return Fail(device.LastError());
        }
        // Upload-heap resources must stay GENERIC_READ. Attempting a
        // VERTEX/INDEX state transition on an UPLOAD heap is invalid.
        else if (tracked != residentState && !dynamic)
        {
            if (!device.TransitionFrameResource(native.Get(), residentState))
                return Fail(device.LastError());
        }
        else if (tracked != residentState)
            return Fail("Dynamic UPLOAD geometry has an invalid tracked resource state");

        if (!device.RetainFrameResource(native.Get()))
            return Fail(device.LastError());
        // Commit only after registration/retention succeeded: a transient
        // allocation failure remains retryable rather than binding stale data.
        dirty = false;
        preparedRecording = device.RecordingSerial();
        return true;
    }
};

struct RendererVertexBufferResource
{
    DX12W3DBufferState state;
    RendererVertexBufferResource(ID3D12Device * device,
                                 const RenderBufferDescription & description)
        : state(device, description) {}
};

struct RendererIndexBufferResource
{
    DX12W3DBufferState state;
    RendererIndexBufferResource(ID3D12Device * device,
                                const RenderBufferDescription & description)
        : state(device, description) {}
};

namespace DX12W3DGeometryBuffers
{
    RenderVertexBufferHandle CreateVertex(ID3D12Device * device,
                                          const RenderBufferDescription & desc)
    {
        lastError.clear();
        if (!device || !desc.Is_Valid())
        {
            Fail("Vertex buffer requires device and nonzero byte size");
            return nullptr;
        }
        DX12W3DVertexLayout layout;
        std::string reason;
        if (!DX12W3DVertexLayouts::DecodeLegacyFVF(desc.legacyVertexFormat, layout, &reason))
        {
            Fail(reason.c_str());
            return nullptr;
        }
        if (desc.byteSize < layout.stride || desc.byteSize % layout.stride)
        {
            Fail("Vertex byte size must contain whole FVF-stride vertices");
            return nullptr;
        }
        auto * handle = new (std::nothrow) RendererVertexBufferResource(device, desc);
        if (handle)
            handle->state.vertexLayout = layout;
        else
            Fail("Unable to allocate DX12 vertex-buffer handle");
        return handle;
    }

    RenderIndexBufferHandle CreateIndex(ID3D12Device * device,
                                        const RenderBufferDescription & desc)
    {
        lastError.clear();
        const unsigned elementBytes = static_cast<unsigned>(desc.indexFormat);
        if (!device || !desc.Is_Valid() ||
            (elementBytes != 2 && elementBytes != 4) ||
            (desc.byteSize % elementBytes) != 0 || desc.legacyVertexFormat != 0)
        {
            Fail("Index buffer needs whole 16-bit/32-bit elements and no vertex FVF");
            return nullptr;
        }
        return new (std::nothrow) RendererIndexBufferResource(device, desc);
    }

    void ReleaseVertex(RenderVertexBufferHandle handle) { delete handle; }
    void ReleaseIndex(RenderIndexBufferHandle handle) { delete handle; }

    void * MapVertex(RenderVertexBufferHandle handle, unsigned offset,
                     unsigned length, unsigned flags)
    {
        void * mapped = nullptr;
        lastError.clear();
        if (!handle || !handle->state.Map(offset, length, flags, mapped))
        {
            if (!handle) Fail("MapVertex received null handle");
            return nullptr;
        }
        return mapped;
    }

    void * MapIndex(RenderIndexBufferHandle handle, unsigned offset,
                    unsigned length, unsigned flags)
    {
        void * mapped = nullptr;
        lastError.clear();
        if (!handle || !handle->state.Map(offset, length, flags, mapped))
        {
            if (!handle) Fail("MapIndex received null handle");
            return nullptr;
        }
        return mapped;
    }

    bool UnmapVertex(RenderVertexBufferHandle handle)
    {
        lastError.clear();
        return handle ? handle->state.Unmap() : Fail("UnmapVertex received null handle");
    }

    bool UnmapIndex(RenderIndexBufferHandle handle)
    {
        lastError.clear();
        return handle ? handle->state.Unmap() : Fail("UnmapIndex received null handle");
    }

    bool BindVertex(DX12PresentationDevice & device,
                    RenderVertexBufferHandle handle, unsigned stream, unsigned stride,
                    unsigned firstVertex, unsigned vertexCount)
    {
        lastError.clear();
        if (!device.ActiveCommandList() || stream >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
            return Fail("Vertex binding requires a native frame and valid stream");
        if (!handle)
        {
            // Legacy SetStreamSource(stream, nullptr, stride) removes a stream.
            // Do not accidentally leave the previous W3D vertex view bound.
            device.ActiveCommandList()->IASetVertexBuffers(stream, 1, nullptr);
            return true;
        }
        if (stride < handle->state.vertexLayout.stride ||
            static_cast<unsigned long long>(firstVertex) * stride >= handle->state.shadow.size())
            return Fail("Invalid W3D DX12 vertex stream, stride or first vertex");
        const unsigned offset = firstVertex * stride;
        const unsigned available = static_cast<unsigned>(handle->state.shadow.size()) - offset;
        const unsigned countBytes = vertexCount
            ? static_cast<unsigned long long>(vertexCount) * stride > available
                  ? 0 : vertexCount * stride
            : available;
        if (!countBytes)
            return Fail("Vertex buffer slice exceeds mapped W3D allocation");
        if (!handle->state.Prepare(device, false))
            return false;
        D3D12_VERTEX_BUFFER_VIEW view = {};
        view.BufferLocation = handle->state.native->GetGPUVirtualAddress() + handle->state.nativeOffset + offset;
        view.SizeInBytes = countBytes;
        view.StrideInBytes = stride;
        device.ActiveCommandList()->IASetVertexBuffers(stream, 1, &view);
        return true;
    }

    bool BindIndex(DX12PresentationDevice & device,
                   RenderIndexBufferHandle handle, unsigned baseVertexIndex,
                   unsigned firstIndex, unsigned indexCount)
    {
        lastError.clear();
        if (!device.ActiveCommandList())
            return Fail("Index binding requires a native DX12 frame");
        if (!handle)
        {
            device.ActiveCommandList()->IASetIndexBuffer(nullptr);
            device.SetBoundBaseVertexIndex(0);
            return true;
        }
        const unsigned elementBytes = handle->state.indexElementBytes;
        if (static_cast<unsigned long long>(firstIndex) * elementBytes >=
            handle->state.shadow.size())
            return Fail("Invalid W3D index range");
        const unsigned offset = firstIndex * elementBytes;
        const unsigned available = static_cast<unsigned>(handle->state.shadow.size()) - offset;
        const unsigned countBytes = indexCount
            ? static_cast<unsigned long long>(indexCount) * elementBytes > available
                  ? 0 : indexCount * elementBytes
            : available;
        if (!countBytes)
            return Fail("Index buffer slice exceeds mapped W3D allocation");
        if (!handle->state.Prepare(device, true))
            return false;
        D3D12_INDEX_BUFFER_VIEW view = {};
        view.BufferLocation = handle->state.native->GetGPUVirtualAddress() + handle->state.nativeOffset + offset;
        view.SizeInBytes = countBytes;
        view.Format = elementBytes == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        device.ActiveCommandList()->IASetIndexBuffer(&view);
        device.SetBoundBaseVertexIndex(baseVertexIndex);
        return true;
    }

    ID3D12Resource * NativeVertexResource(RenderVertexBufferHandle handle)
    {
        return handle ? handle->state.native.Get() : nullptr;
    }

    ID3D12Resource * NativeIndexResource(RenderIndexBufferHandle handle)
    {
        return handle ? handle->state.native.Get() : nullptr;
    }

    unsigned NativeVertexOffset(RenderVertexBufferHandle handle)
    {
        return handle ? handle->state.nativeOffset : 0;
    }

    unsigned NativeIndexOffset(RenderIndexBufferHandle handle)
    {
        return handle ? handle->state.nativeOffset : 0;
    }

    const DX12W3DVertexLayout * NativeVertexLayout(RenderVertexBufferHandle handle)
    {
        return handle ? &handle->state.vertexLayout : nullptr;
    }

    unsigned NativeIndexElementBytes(RenderIndexBufferHandle handle)
    {
        return handle ? handle->state.indexElementBytes : 0;
    }

    unsigned VertexByteSize(RenderVertexBufferHandle handle)
    {
        return handle ? static_cast<unsigned>(handle->state.shadow.size()) : 0;
    }

    unsigned IndexByteSize(RenderIndexBufferHandle handle)
    {
        return handle ? static_cast<unsigned>(handle->state.shadow.size()) : 0;
    }

    bool IsVertexMapped(RenderVertexBufferHandle handle)
    {
        return handle && handle->state.mapped;
    }

    bool IsIndexMapped(RenderIndexBufferHandle handle)
    {
        return handle && handle->state.mapped;
    }

    const char * LastError() { return lastError.c_str(); }
}
