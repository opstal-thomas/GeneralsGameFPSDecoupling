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
#include <mutex>
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
    // Dynamic suballocations are immutable for the lifetime of a submitted
    // command list. Only completed queue fences permit upload-page recycling.
    constexpr unsigned UploadPageBytes = 1024u * 1024u;
    struct UploadPage
    {
        ComPtr<ID3D12Device> owner;
        ComPtr<ID3D12Resource> resource;
        unsigned capacity = 0;
        unsigned cursor = 0;
        unsigned long long serial = 0;
        unsigned long long retireFence = 0;
    };
    std::mutex uploadMutex;
    std::vector<UploadPage> uploadPages;
    bool ReserveUpload(DX12PresentationDevice & device, unsigned length,
                       ComPtr<ID3D12Resource> & resource, unsigned & offset)
    {
        const auto serial = device.RecordingSerial();
        if (!serial || !length)
            return Fail("Transient geometry allocation requires an active frame");
        const auto completed = device.CompletedFenceValue();
        if (completed == UINT64_MAX)
            return Fail("Transient geometry fence is invalid");
        const auto submitted = device.LastSubmittedFenceValue();
        if (length > UINT32_MAX - 15u)
            return Fail("Transient geometry size overflow");
        const unsigned aligned = (length + 15u) & ~15u;
        std::lock_guard<std::mutex> lock(uploadMutex);
        for (auto & page : uploadPages)
        {
            if (page.owner.Get() != device.NativeDevice())
                continue;
            if (page.serial != serial)
            {
                if (page.serial && page.retireFence == 0)
                    page.retireFence = submitted;
                if (page.retireFence > completed)
                    continue;
                page.cursor = 0;
                page.serial = serial;
                page.retireFence = 0;
            }
            if (aligned <= page.capacity - page.cursor)
            {
                offset = page.cursor;
                page.cursor += aligned;
                resource = page.resource;
                return true;
            }
        }
        UploadPage page;
        page.owner = device.NativeDevice();
        page.capacity = std::max(UploadPageBytes, aligned);
        if (!MakeBuffer(page.owner.Get(), page.capacity, D3D12_HEAP_TYPE_UPLOAD,
                        D3D12_RESOURCE_STATE_GENERIC_READ, page.resource))
            return Fail("Creating transient upload page failed");
        page.cursor = aligned;
        page.serial = serial;
        resource = page.resource;
        offset = 0;
        uploadPages.push_back(std::move(page));
        return true;
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
    bool dynamic = false;
    bool mapped = false;
    bool readOnly = false;
    bool dirty = true;

    explicit DX12W3DBufferState(ID3D12Device * device,
                                const RenderBufferDescription & desc)
        : owner(device), shadow(desc.byteSize, 0),
          format(desc.legacyVertexFormat),
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

        if (!native || dirty)
        {
            ComPtr<ID3D12Resource> newNative;
            if (dynamic)
            {
                // DISCARD and NO_OVERWRITE each receive an immutable slice.
                if (!ReserveUpload(device, static_cast<unsigned>(shadow.size()),
                                   newNative, nativeOffset))
                    return false;
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
            dirty = false;
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
        return new (std::nothrow) RendererVertexBufferResource(device, desc);
    }

    RenderIndexBufferHandle CreateIndex(ID3D12Device * device,
                                        const RenderBufferDescription & desc)
    {
        lastError.clear();
        if (!device || !desc.Is_Valid() || (desc.byteSize & 1u) != 0 ||
            desc.legacyVertexFormat != 0)
        {
            Fail("Index buffer must have an even byte count and 16-bit W3D index format");
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
        if (!stride ||
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
        if (static_cast<unsigned long long>(firstIndex) * sizeof(std::uint16_t) >=
            handle->state.shadow.size())
            return Fail("Invalid W3D index range");
        const unsigned offset = firstIndex * sizeof(std::uint16_t);
        const unsigned available = static_cast<unsigned>(handle->state.shadow.size()) - offset;
        const unsigned countBytes = indexCount
            ? static_cast<unsigned long long>(indexCount) * 2 > available ? 0 : indexCount * 2
            : available;
        if (!countBytes)
            return Fail("Index buffer slice exceeds mapped W3D allocation");
        if (!handle->state.Prepare(device, true))
            return false;
        D3D12_INDEX_BUFFER_VIEW view = {};
        view.BufferLocation = handle->state.native->GetGPUVirtualAddress() + handle->state.nativeOffset + offset;
        view.SizeInBytes = countBytes;
        view.Format = DXGI_FORMAT_R16_UINT;
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
