/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/

// R01/R02: check actual WARP GPU allocation, index format, byte-range mapping,
// IA bindings, pooled dynamic snapshot isolation, and fence-gated page reuse.
#include "../DX12PresentationDevice.h"
#include "../DX12W3DGeometryBuffers.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

using Microsoft::WRL::ComPtr;

namespace
{
    struct TestWindow
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND hwnd = nullptr;
        bool registered = false;
        bool Create()
        {
            WNDCLASSW cls = {};
            cls.hInstance = instance;
            cls.lpfnWndProc = DefWindowProcW;
            cls.lpszClassName = L"W3DDX12R01WARP";
            registered = RegisterClassW(&cls) != 0;
            if (!registered)
                return false;
            hwnd = CreateWindowExW(0, cls.lpszClassName, L"W3D R01 native buffer smoke",
                                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                    300, 240, nullptr, nullptr, instance, nullptr);
            if (!hwnd)
                return false;
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            return true;
        }
        ~TestWindow()
        {
            if (hwnd && IsWindow(hwnd))
                DestroyWindow(hwnd);
            if (registered)
                UnregisterClassW(L"W3DDX12R01WARP", instance);
        }
    };

    bool Check(bool ok, const char * what)
    {
        if (!ok)
            std::cerr << "DX12-R01: " << what << " FAILED: "
                      << DX12W3DGeometryBuffers::LastError() << '\n';
        return ok;
    }

    bool MakeReadback(ID3D12Device * device, unsigned bytes,
                      ComPtr<ID3D12Resource> & output)
    {
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.SampleDesc.Count = 1;
        return SUCCEEDED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&output)));
    }

    bool VerifyBytes(ID3D12Resource * buffer, const void * expected, unsigned size)
    {
        const D3D12_RANGE range = {0, size};
        void * mapped = nullptr;
        if (FAILED(buffer->Map(0, &range, &mapped)))
            return false;
        const bool equal = std::memcmp(mapped, expected, size) == 0;
        const D3D12_RANGE noWrite = {0, 0};
        buffer->Unmap(0, &noWrite);
        return equal;
    }

    bool HeapType(ID3D12Resource * resource, D3D12_HEAP_TYPE expected)
    {
        if (!resource)
            return false;
        D3D12_HEAP_PROPERTIES heap = {};
        D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
        return SUCCEEDED(resource->GetHeapProperties(&heap, &flags)) &&
               heap.Type == expected;
    }
}

int wmain()
{
    using namespace DX12W3DGeometryBuffers;
    TestWindow window;
    if (!Check(window.Create(), "Create HWND"))
        return 1;
    DX12PresentationDevice device;
    if (!Check(device.Initialize(window.hwnd, 160, 120, true), "Init WARP"))
    {
        std::cerr << device.LastError() << '\n';
        return 1;
    }

    RenderBufferDescription vertexDesc;
    vertexDesc.byteSize = 48;
    vertexDesc.legacyVertexFormat = 0x002; // W3D XYZ vertices, four * 3 floats
    RenderBufferDescription indexDesc;
    indexDesc.byteSize = 12;
    vertexDesc.usageFlags = RENDER_BUFFER_STATIC;
    indexDesc.usageFlags = RENDER_BUFFER_STATIC;

    auto vertex = CreateVertex(device.NativeDevice(), vertexDesc);
    auto index = CreateIndex(device.NativeDevice(), indexDesc);
    RenderBufferDescription dynamicDesc = vertexDesc;
    dynamicDesc.usageFlags = RENDER_BUFFER_DYNAMIC;
    auto dynamicVertex = CreateVertex(device.NativeDevice(), dynamicDesc);
    auto dynamicIndex = CreateIndex(device.NativeDevice(),
                                   [&]() { RenderBufferDescription d = indexDesc;
                                            d.usageFlags = RENDER_BUFFER_DYNAMIC; return d; }());

    if (!Check(vertex && index && dynamicVertex && dynamicIndex, "Create typed geometry") ||
        !Check(!CreateIndex(device.NativeDevice(), vertexDesc),
               "Reject vertex format on index buffer") ||
        !Check(!MapVertex(vertex, 49, 0, RENDER_MAP_NONE), "Reject range overflow"))
        return 1;
    const std::array<float, 12> vertexBytes = {
        -0.5f, -0.5f, 0.0f, -0.5f, 0.5f, 0.0f,
         0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f
    };
    const std::array<std::uint16_t, 6> indices = {0, 1, 2, 2, 1, 3};
    void * vb = MapVertex(vertex, 0, 0, RENDER_MAP_NONE);
    if (!Check(vb != nullptr, "Map whole vertex buffer") ||
        !Check(!MapVertex(vertex, 0, 0, RENDER_MAP_NONE), "Reject nested vertex map"))
        return 1;
    std::memcpy(vb, vertexBytes.data(), vertexDesc.byteSize);
    if (!Check(UnmapVertex(vertex), "Unmap static VB") ||
        !Check(!UnmapVertex(vertex), "Reject double unmap") ||
        !Check(!MapVertex(vertex, 0, 0, RENDER_MAP_DISCARD),
               "Reject DISCARD on static resource"))
        return 1;
    auto * ib = static_cast<std::uint16_t *>(
        MapIndex(index, 0, indexDesc.byteSize, RENDER_MAP_NONE));
    if (!Check(ib != nullptr, "Map static IB"))
        return 1;
    std::memcpy(ib, indices.data(), indexDesc.byteSize);
    if (!Check(UnmapIndex(index), "Unmap static IB"))
        return 1;

    auto * dv = static_cast<unsigned char *>(
        MapVertex(dynamicVertex, 0, 0, RENDER_MAP_DISCARD));
    auto * di = static_cast<unsigned char *>(
        MapIndex(dynamicIndex, 0, 0, RENDER_MAP_DISCARD));
    if (!Check(dv && di, "Map dynamic DISCARD"))
        return 1;
    std::memcpy(dv, vertexBytes.data(), vertexDesc.byteSize);
    std::memcpy(di, indices.data(), indexDesc.byteSize);
    if (!Check(UnmapVertex(dynamicVertex) && UnmapIndex(dynamicIndex),
               "Unmap dynamic resource version"))
        return 1;

    ComPtr<ID3D12Resource> vertexReadback, indexReadback;
    if (!Check(MakeReadback(device.NativeDevice(), vertexDesc.byteSize, vertexReadback) &&
               MakeReadback(device.NativeDevice(), indexDesc.byteSize, indexReadback),
               "Create real GPU readback buffers"))
        return 1;
    const float clear[] = {0.09f, 0.11f, 0.25f, 1.0f};
    if (!Check(device.BeginFrame(clear), "Begin native GPU recording") ||
        !Check(BindVertex(device, vertex, 0, 12), "Bind static GPU VB") ||
        !Check(BindIndex(device, index, 7), "Bind static GPU IB") ||
        !Check(device.BoundBaseVertexIndex() == 7, "Record W3D base vertex") ||
        !Check(HeapType(NativeVertexResource(vertex), D3D12_HEAP_TYPE_DEFAULT) &&
               HeapType(NativeIndexResource(index), D3D12_HEAP_TYPE_DEFAULT),
               "Static buffers use GPU-local DEFAULT heaps"))
        return 1;
    if (!Check(device.TransitionFrameResource(
                   NativeVertexResource(vertex), D3D12_RESOURCE_STATE_COPY_SOURCE),
               "Barrier static VB to COPY_SOURCE") ||
        !Check(device.TransitionFrameResource(
                   NativeIndexResource(index), D3D12_RESOURCE_STATE_COPY_SOURCE),
               "Barrier static IB to COPY_SOURCE"))
        return 1;
    device.ActiveCommandList()->CopyBufferRegion(
        vertexReadback.Get(), 0, NativeVertexResource(vertex), 0, vertexDesc.byteSize);
    device.ActiveCommandList()->CopyBufferRegion(
        indexReadback.Get(), 0, NativeIndexResource(index), 0, indexDesc.byteSize);
    if (!Check(device.TransitionFrameResource(
                   NativeVertexResource(vertex),
                   D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
               "Restore vertex read state") ||
        !Check(device.TransitionFrameResource(
                   NativeIndexResource(index), D3D12_RESOURCE_STATE_INDEX_BUFFER),
               "Restore index read state") ||
        !Check(BindVertex(device, dynamicVertex, 1, 12, 1, 2),
               "Bind dynamic vertex slice") ||
        !Check(BindIndex(device, dynamicIndex, 3, 1, 3),
               "Bind dynamic index slice") ||
        !Check(device.BoundBaseVertexIndex() == 3, "Dynamic index base vertex") ||
        !Check(HeapType(NativeVertexResource(dynamicVertex), D3D12_HEAP_TYPE_UPLOAD) &&
               HeapType(NativeIndexResource(dynamicIndex), D3D12_HEAP_TYPE_UPLOAD),
               "Dynamic geometry uses CPU-visible D3D12 UPLOAD heaps"))
        return 1;

    // R02: both streams share an UPLOAD page, but their independent byte
    // ranges and every subsequent dynamic generation must remain immutable.
    ID3D12Resource * firstGpu = NativeVertexResource(dynamicVertex);
    const unsigned firstVertexOffset = NativeVertexOffset(dynamicVertex);
    const unsigned firstIndexOffset = NativeIndexOffset(dynamicIndex);
    ComPtr<ID3D12Resource> firstVertexReadback, secondVertexReadback, dynamicIndexReadback;
    if (!Check(NativeVertexSliceBytes(dynamicVertex) == 24 &&
               NativeVertexSourceOffset(dynamicVertex) == 12 &&
               NativeIndexSliceBytes(dynamicIndex) == 6 &&
               NativeIndexSourceOffset(dynamicIndex) == 2,
               "R02 dynamic vertex and index snapshots contain only requested ranges") ||
        !Check(firstGpu == NativeIndexResource(dynamicIndex),
               "Small vertex and index snapshots share an upload page") ||
        !Check(firstVertexOffset != firstIndexOffset,
               "Dynamic vertex and index offsets cannot alias") ||
        !Check(MakeReadback(device.NativeDevice(), 24, firstVertexReadback) &&
               MakeReadback(device.NativeDevice(), vertexDesc.byteSize, secondVertexReadback) &&
               MakeReadback(device.NativeDevice(), 6, dynamicIndexReadback),
               "Allocate pooled geometry GPU readbacks"))
        return 1;
    device.ActiveCommandList()->CopyBufferRegion(
        firstVertexReadback.Get(), 0, firstGpu, firstVertexOffset, 24);
    device.ActiveCommandList()->CopyBufferRegion(
        dynamicIndexReadback.Get(), 0, NativeIndexResource(dynamicIndex),
        firstIndexOffset, 6);

    // Emulate multiple dynamic writes/binds in one frame; the previous GPU
    // generation must remain valid through the command-list fence.
    auto * changed = static_cast<unsigned char *>(
        MapVertex(dynamicVertex, 16, 8, RENDER_MAP_NO_OVERWRITE));
    if (!Check(changed != nullptr, "Partial NO_OVERWRITE map"))
        return 1;
    std::memset(changed, 0x5A, 8);
    if (!Check(UnmapVertex(dynamicVertex) &&
               BindVertex(device, dynamicVertex, 1, 12), "Rebind new dynamic generation") ||
        !Check(firstGpu == NativeVertexResource(dynamicVertex),
               "Dynamic writes share an upload page") ||
        !Check(NativeVertexOffset(dynamicVertex) != firstVertexOffset &&
               NativeVertexOffset(dynamicVertex) != firstIndexOffset,
               "NO_OVERWRITE receives a nonoverlapping slice") ||
        !Check(device.InFlightResourceCount() >= 6, "Old generations and staging pinned"))
        return 1;
    device.ActiveCommandList()->CopyBufferRegion(
        secondVertexReadback.Get(), 0, NativeVertexResource(dynamicVertex),
        NativeVertexOffset(dynamicVertex), vertexDesc.byteSize);

    std::array<unsigned char, 48> changedDynamicVertexBytes = {};
    std::memcpy(changedDynamicVertexBytes.data(), vertexBytes.data(), vertexDesc.byteSize);
    std::memset(changedDynamicVertexBytes.data() + 16, 0x5A, 8);

    // A partial static write after binding must not modify the version
    // referenced by the earlier GPU readback or the first indexed draw.
    // The replacement DEFAULT allocation is uploaded on this same list.
    auto * patch = static_cast<unsigned char *>(
        MapVertex(vertex, 12, 4, RENDER_MAP_NONE));
    if (!Check(patch != nullptr, "Map partial static vertex range"))
        return 1;
    const unsigned char update[] = {0x35, 0x76, 0xA2, 0x19};
    std::memcpy(patch, update, sizeof(update));
    if (!Check(UnmapVertex(vertex), "Commit partial static update"))
        return 1;
    ID3D12Resource * previousStatic = NativeVertexResource(vertex);
    ComPtr<ID3D12Resource> updatedReadback;
    if (!Check(MakeReadback(device.NativeDevice(), vertexDesc.byteSize, updatedReadback) &&
               BindVertex(device, vertex, 0, 12), "Re-upload changed static geometry") ||
        !Check(NativeVertexResource(vertex) != previousStatic,
               "Static updates allocate a new DEFAULT resource generation") ||
        !Check(device.TransitionFrameResource(
                   NativeVertexResource(vertex), D3D12_RESOURCE_STATE_COPY_SOURCE),
               "Transition updated static GPU resource to readback"))
        return 1;
    device.ActiveCommandList()->CopyBufferRegion(
        updatedReadback.Get(), 0, NativeVertexResource(vertex), 0, vertexDesc.byteSize);
    if (!Check(device.TransitionFrameResource(
                   NativeVertexResource(vertex),
                   D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
               "Restore updated static geometry state"))
        return 1;
    std::array<unsigned char, 48> updatedVertexBytes = {};
    std::memcpy(updatedVertexBytes.data(), vertexBytes.data(), vertexDesc.byteSize);
    std::memcpy(updatedVertexBytes.data() + 12, update, sizeof(update));

    if (!Check(BindVertex(device, nullptr, 1, 0),
               "Unbind W3D vertex stream without stale IA view") ||
        !Check(BindIndex(device, nullptr, 0),
               "Unbind W3D index buffer") ||
        !Check(device.BoundBaseVertexIndex() == 0,
               "Unbinding indices resets their base vertex"))
        return 1;

    // Destroy all four opaque handles before ExecuteCommandLists. The actual
    // GPU buffers/staging are held by the presentation frame-fence retirement.
    ReleaseVertex(vertex);
    ReleaseIndex(index);
    ReleaseVertex(dynamicVertex);
    ReleaseIndex(dynamicIndex);
    if (!Check(device.EndFrame(0, true), "Submit and present after owner release") ||
        !Check(device.WaitForIdle(), "Wait for native GPU resource retirement") ||
        !Check(VerifyBytes(vertexReadback.Get(), vertexBytes.data(), vertexDesc.byteSize),
               "DEFAULT heap vertex bytes match source on WARP") ||
        !Check(VerifyBytes(indexReadback.Get(), indices.data(), indexDesc.byteSize),
               "DEFAULT heap index bytes match source on WARP") ||
        !Check(VerifyBytes(updatedReadback.Get(), updatedVertexBytes.data(),
                           vertexDesc.byteSize),
               "Partial static update matches new GPU allocation without corrupting old version") ||
        !Check(VerifyBytes(firstVertexReadback.Get(),
                           static_cast<const unsigned char *>(static_cast<const void *>(vertexBytes.data())) + 12, 24),
               "First pooled dynamic snapshot survives NO_OVERWRITE on WARP") ||
        !Check(VerifyBytes(secondVertexReadback.Get(), changedDynamicVertexBytes.data(),
                           vertexDesc.byteSize),
               "Updated pooled dynamic slice contains the new bytes on WARP") ||
        !Check(VerifyBytes(dynamicIndexReadback.Get(), indices.data() + 1, 6),
               "Shared-page index bytes are not corrupted by vertex updates") ||
        !Check(device.InFlightResourceCount() == 0,
               "GPU frame-fence retirement has released staging generations"))
    {
        std::cerr << device.LastError() << '\n';
        return 1;
    }

    // A completed fence allows recycling the existing page without allocating
    // another megabyte, including after a swap-chain resize.
    auto recycled = CreateVertex(device.NativeDevice(), dynamicDesc);
    if (!Check(recycled != nullptr, "Create post-fence dynamic geometry"))
        return 1;
    auto * recycledBytes = static_cast<unsigned char *>(
        MapVertex(recycled, 0, 0, RENDER_MAP_DISCARD));
    if (!Check(recycledBytes != nullptr, "Map recycled DISCARD range"))
        return 1;
    std::memcpy(recycledBytes, vertexBytes.data(), vertexDesc.byteSize);
    if (!Check(UnmapVertex(recycled), "Commit recycled dynamic bytes") ||
        !Check(device.Resize(128, 96), "Resize while resources retired") ||
        !Check(device.BeginFrame(clear), "Begin frame after resize") ||
        !Check(BindVertex(device, recycled, 0, 12), "Rebind on recycled page") ||
        !Check(NativeVertexResource(recycled) == firstGpu &&
               NativeVertexOffset(recycled) == firstVertexOffset,
               "Recycle the pooled upload page only after the submission fence") ||
        !Check(device.EndFrame(0), "Submit resized frame with recycled geometry") ||
        !Check(device.WaitForIdle(), "Final fence"))
        return 1;
    ReleaseVertex(recycled);

    // A dynamic W3D handle can remain alive and get rebound *without* being
    // mapped again. Recycling its first page must never change the data seen
    // by that later bind. This catches the pre-R02 cross-frame stale slice.
    auto longLived = CreateVertex(device.NativeDevice(), dynamicDesc);
    auto other = CreateVertex(device.NativeDevice(), dynamicDesc);
    std::array<unsigned char, 48> stableBytes = {}, otherBytes = {};
    stableBytes.fill(0x27);
    otherBytes.fill(0xCA);
    if (!Check(longLived && other, "Create cross-frame dynamic rings"))
        return 1;
    auto * stableMap = static_cast<unsigned char *>(
        MapVertex(longLived, 0, 0, RENDER_MAP_DISCARD));
    if (!Check(stableMap != nullptr, "Map first persistent dynamic handle"))
        return 1;
    std::memcpy(stableMap, stableBytes.data(), stableBytes.size());
    if (!Check(UnmapVertex(longLived), "Unmap persistent dynamic handle") ||
        !Check(device.BeginFrame(clear), "Begin persistent-handle first frame") ||
        !Check(BindVertex(device, longLived, 0, 12), "First bind persistent handle") ||
        !Check(device.EndFrame(0), "Submit first persistent-handle frame") ||
        !Check(device.WaitForIdle(), "Complete first persistent-handle frame"))
        return 1;

    auto * otherMap = static_cast<unsigned char *>(
        MapVertex(other, 0, 0, RENDER_MAP_DISCARD));
    if (!Check(otherMap != nullptr, "Map second dynamic handle"))
        return 1;
    std::memcpy(otherMap, otherBytes.data(), otherBytes.size());
    ComPtr<ID3D12Resource> stableReadback, otherReadback;
    if (!Check(UnmapVertex(other) &&
               MakeReadback(device.NativeDevice(), dynamicDesc.byteSize, stableReadback) &&
               MakeReadback(device.NativeDevice(), dynamicDesc.byteSize, otherReadback),
               "Allocate cross-frame WARP GPU readbacks") ||
        !Check(device.BeginFrame(clear), "Begin cross-frame page-recycling test") ||
        !Check(BindVertex(device, other, 0, 12), "Rebind other on recycled page") ||
        !Check(BindVertex(device, longLived, 0, 12),
               "Rebind unchanged dynamic buffer after prior page reuse") ||
        !Check(NativeVertexResource(other) == NativeVertexResource(longLived) &&
               NativeVertexOffset(other) != NativeVertexOffset(longLived),
               "Unchanged cross-frame vertex data resnapshotted to a distinct slice"))
        return 1;
    device.ActiveCommandList()->CopyBufferRegion(
        stableReadback.Get(), 0, NativeVertexResource(longLived),
        NativeVertexOffset(longLived), dynamicDesc.byteSize);
    device.ActiveCommandList()->CopyBufferRegion(
        otherReadback.Get(), 0, NativeVertexResource(other),
        NativeVertexOffset(other), dynamicDesc.byteSize);
    if (!Check(device.EndFrame(0), "Submit cross-frame dynamic page reuse") ||
        !Check(device.WaitForIdle(), "Wait for cross-frame readback") ||
        !Check(VerifyBytes(stableReadback.Get(), stableBytes.data(), stableBytes.size()) &&
               VerifyBytes(otherReadback.Get(), otherBytes.data(), otherBytes.size()),
               "Two dynamic W3D handles survive GPU page reuse across frames"))
        return 1;
    ReleaseVertex(longLived);
    ReleaseVertex(other);

    // R02 growth is demand-driven rather than a hard mesh cap: five roughly
    // 256 KiB immutable generations must span more than one UPLOAD page.
    RenderBufferDescription largeDynamic = dynamicDesc;
    largeDynamic.byteSize = 264000; // 22,000 tightly packed XYZ vertices
    auto oversized = CreateVertex(device.NativeDevice(), largeDynamic);
    if (!Check(oversized != nullptr && device.BeginFrame(clear),
               "Begin dynamic upload page growth test"))
        return 1;
    ID3D12Resource * firstLargePage = nullptr;
    for (unsigned generation = 0; generation < 5; ++generation)
    {
        auto * mapped = static_cast<unsigned char *>(
            MapVertex(oversized, 0, 0, RENDER_MAP_DISCARD));
        if (!Check(mapped != nullptr, "Map large dynamic generation"))
            return 1;
        std::memset(mapped, static_cast<int>(generation + 1), largeDynamic.byteSize);
        if (!Check(UnmapVertex(oversized) &&
                   BindVertex(device, oversized, 0, 12),
                   "Commit and bind large dynamic generation") ||
            !Check((NativeVertexOffset(oversized) % 16u) == 0u,
                   "Transient large generation is 16-byte aligned"))
            return 1;
        if (generation == 0)
            firstLargePage = NativeVertexResource(oversized);
        if (generation == 4 &&
            !Check(NativeVertexResource(oversized) != firstLargePage,
                   "Large ring overflow allocated another page"))
            return 1;
    }
    if (!Check(device.TransientGeometryPageCount() >= 2 &&
               device.TransientGeometryResidentBytes() >= 2ull * 1024 * 1024,
               "Demand-based transient GPU page growth") ||
        !Check(device.EndFrame(0), "Submit multi-page transient generation") ||
        !Check(device.WaitForIdle(), "Retire multi-page geometry"))
        return 1;
    ReleaseVertex(oversized);

    // The device owns the pool, not process globals. Full shutdown/reset must
    // release the upload pages even if the old resource handles were released.
    device.Shutdown();
    if (!Check(device.TransientGeometryPageCount() == 0 &&
               device.TransientGeometryResidentBytes() == 0,
               "Shutdown clears device-owned transient geometry") ||
        !Check(device.Initialize(window.hwnd, 128, 96, true),
               "Reinitialize native WARP device after upload growth") ||
        !Check(device.TransientGeometryPageCount() == 0,
               "Recreated DX12 device starts with an empty upload pool") ||
        !Check(device.BeginFrame(clear) && device.EndFrame(0) && device.WaitForIdle(),
               "Recreated device submits a clean frame"))
        return 1;
    device.Shutdown();
    std::cout << "R01/R02 PASS: native static/default and pooled dynamic/upload resources, "
                 "mapped byte ranges, indexed IA views and WARP GPU readback\n";
    return 0;
}
