/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#include "DX12W3DSurfaceTransfers.h"
#include "DX12PresentationDevice.h"
#include "DX12W3DRenderTargets.h"
#include "DX12W3DSurfaces.h"
#include "DX12W3DTextures.h"

#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr unsigned BytesPerPixel = 4;
    constexpr unsigned MaxTransferBytes = 256u * 1024u * 1024u;
    const D3D12_RESOURCE_STATES Sampled =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    bool ValidRect(const RenderSurfaceRect & rect, unsigned width, unsigned height)
    {
        return rect.left >= 0 && rect.top >= 0 &&
            rect.right > rect.left && rect.bottom > rect.top &&
            static_cast<unsigned>(rect.right) <= width &&
            static_cast<unsigned>(rect.bottom) <= height &&
            static_cast<std::uint64_t>(rect.right - rect.left) *
                (rect.bottom - rect.top) * BytesPerPixel <= MaxTransferBytes;
    }

    RenderSurfaceRect Full(const RenderSurfaceDescription & desc)
    {
        return {0, 0, static_cast<int>(desc.width), static_cast<int>(desc.height)};
    }

    D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES h = {};
        h.Type = type;
        h.CreationNodeMask = h.VisibleNodeMask = 1;
        return h;
    }

    bool CreateBuffer(DX12PresentationDevice & frame, UINT64 bytes,
                      D3D12_HEAP_TYPE type, ComPtr<ID3D12Resource> & result)
    {
        if (!frame.NativeDevice() || !bytes || bytes > MaxTransferBytes + 1024ull * 1024ull)
            return false;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = 1;
        desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const auto heap = Heap(type);
        return SUCCEEDED(frame.NativeDevice()->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            type == D3D12_HEAP_TYPE_READBACK ?
                D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&result)));
    }

    D3D12_TEXTURE_COPY_LOCATION Image(ID3D12Resource * resource)
    {
        D3D12_TEXTURE_COPY_LOCATION location = {};
        location.pResource = resource;
        location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        location.SubresourceIndex = 0;
        return location;
    }

    D3D12_TEXTURE_COPY_LOCATION BufferImage(ID3D12Resource * buffer,
                                              const D3D12_PLACED_SUBRESOURCE_FOOTPRINT & footprint)
    {
        D3D12_TEXTURE_COPY_LOCATION location = {};
        location.pResource = buffer;
        location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        location.PlacedFootprint = footprint;
        return location;
    }
}
struct DX12W3DSurfaceTransfers::Impl
{
    struct Mapping
    {
        RenderSurfaceRect region = {};
        std::vector<std::uint8_t> pixels;
        bool readOnly = false;
    };
    std::unordered_map<RenderSurfaceHandle, Mapping> mapped;
    std::string error;
    bool Fail(const char * message) { error = message; return false; }

    bool SurfaceInfo(DX12W3DRenderTargets & targets, RenderSurfaceHandle handle,
                     RenderSurfaceDescription & desc)
    {
        desc = RenderSurfaceDescription();
        const bool valid = targets.Owns(handle) ?
            targets.Describe(handle, desc) : DX12W3DSurfaces::Describe(handle, desc);
        return valid || Fail("R08 requires an owned CPU or GPU W3D surface");
    }

    bool Restore(DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
                 RenderSurfaceHandle handle, ID3D12Resource * native)
    {
        const auto state = native == frame.CurrentBackBuffer() ||
            native == targets.BoundColorResource(frame)
            ? D3D12_RESOURCE_STATE_RENDER_TARGET : Sampled;
        return targets.TransitionTransfer(frame, handle, state) ||
            Fail(targets.LastError());
    }

    bool Read(DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
              RenderSurfaceHandle handle, const RenderSurfaceRect & region,
              std::vector<std::uint8_t> & output)
    {
        output.clear();
        if (!frame.IsRecording() || !frame.ActiveCommandList() ||
            !targets.Owns(handle))
            return Fail("R08 GPU readback needs a live surface and recording");
        ID3D12Resource * native = nullptr;
        if (!targets.TransferResource(frame, handle, false, native))
            return Fail(targets.LastError());
        const auto d = native->GetDesc();
        if (!ValidRect(region, static_cast<unsigned>(d.Width), d.Height))
            return Fail("R08 GPU readback region is outside the color surface");
        const UINT width = region.right - region.left;
        const UINT height = region.bottom - region.top;
        D3D12_RESOURCE_DESC image = d;
        image.Width = width; image.Height = height;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT rows = 0;
        UINT64 rowBytes = 0, total = 0;
        frame.NativeDevice()->GetCopyableFootprints(
            &image, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
        ComPtr<ID3D12Resource> staging;
        if (!total || rows != height || !CreateBuffer(frame, total,
                                                     D3D12_HEAP_TYPE_READBACK, staging))
            return Fail("Allocating GPU readback footprint failed");
        if (!targets.TransitionTransfer(frame, handle, D3D12_RESOURCE_STATE_COPY_SOURCE))
            return Fail(targets.LastError());
        const auto dst = BufferImage(staging.Get(), footprint);
        const auto src = Image(native);
        D3D12_BOX box = {
            static_cast<UINT>(region.left), static_cast<UINT>(region.top), 0,
            static_cast<UINT>(region.right), static_cast<UINT>(region.bottom), 1
        };
        frame.ActiveCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
        if (!Restore(frame, targets, handle, native))
            return false;
        // Flush submits all earlier 3D/2D writes in *this* frame before
        // reading the copied bytes, not the previous frame's backbuffer.
        if (!frame.FlushAndContinue() || !targets.RebindAfterFlush(frame))
            return Fail(frame.LastError());
        output.resize(static_cast<std::size_t>(width) * height * 4u);
        void * bytes = nullptr;
        if (FAILED(staging->Map(0, nullptr, &bytes)) || !bytes)
            return Fail("Mapping fenced R08 readback buffer failed");
        const auto * base = static_cast<const std::uint8_t *>(bytes);
        for (UINT y = 0; y < height; ++y)
        {
            auto * dest = output.data() + static_cast<std::size_t>(y) * width * 4u;
            const auto * srcRow = base + footprint.Offset +
                static_cast<std::size_t>(y) * footprint.Footprint.RowPitch;
            std::memcpy(dest, srcRow, width * 4u);
            if (d.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
                for (UINT x = 0; x < width; ++x)
                    std::swap(dest[x * 4], dest[x * 4 + 2]);
        }
        const D3D12_RANGE none = {0, 0};
        staging->Unmap(0, &none);
        return true;
    }

    bool Write(DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
               RenderSurfaceHandle handle, const RenderSurfaceRect & region,
               const unsigned char * input, unsigned stride)
    {
        if (!frame.IsRecording() || !input || !targets.Owns(handle))
            return Fail("R08 upload requires active frame and GPU surface");
        ID3D12Resource * native = nullptr;
        if (!targets.TransferResource(frame, handle, true, native))
            return Fail(targets.LastError());
        const auto d = native->GetDesc();
        if (!ValidRect(region, static_cast<unsigned>(d.Width), d.Height) ||
            stride < static_cast<unsigned>(region.right - region.left) * 4u)
            return Fail("Invalid R08 CPU-to-GPU upload bounds or stride");
        const UINT width = region.right - region.left;
        const UINT height = region.bottom - region.top;
        D3D12_RESOURCE_DESC image = d;
        image.Width = width; image.Height = height;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT rows = 0;
        UINT64 rowBytes = 0, total = 0;
        frame.NativeDevice()->GetCopyableFootprints(
            &image, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
        ComPtr<ID3D12Resource> upload;
        if (!total || rows != height ||
            !CreateBuffer(frame, total, D3D12_HEAP_TYPE_UPLOAD, upload))
            return Fail("Allocating DX12 R08 upload footprint failed");
        void * mappedBytes = nullptr;
        if (FAILED(upload->Map(0, nullptr, &mappedBytes)) || !mappedBytes)
            return Fail("Mapping R08 upload buffer failed");
        auto * base = static_cast<std::uint8_t *>(mappedBytes);
        for (UINT y = 0; y < height; ++y)
        {
            auto * destRow = base + footprint.Offset +
                static_cast<std::size_t>(y) * footprint.Footprint.RowPitch;
            std::memcpy(destRow, input + static_cast<std::size_t>(y) * stride,
                        width * 4u);
            if (d.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
                for (UINT x = 0; x < width; ++x)
                    std::swap(destRow[x * 4], destRow[x * 4 + 2]);
        }
        upload->Unmap(0, nullptr);
        if (!targets.TransitionTransfer(frame, handle, D3D12_RESOURCE_STATE_COPY_DEST))
            return Fail(targets.LastError());
        const auto src = BufferImage(upload.Get(), footprint);
        const auto dst = Image(native);
        frame.ActiveCommandList()->CopyTextureRegion(
            &dst, static_cast<UINT>(region.left), static_cast<UINT>(region.top),
            0, &src, nullptr);
        if (!Restore(frame, targets, handle, native) ||
            !frame.RetainFrameResource(upload.Get()))
            return Fail(frame.LastError());
        return true;
    }

    bool FillCPU(RenderSurfaceHandle target, const RenderSurfaceRect & rect,
                 const std::vector<std::uint8_t> & pixels)
    {
        RenderSurfaceMap map;
        if (!DX12W3DSurfaces::Lock(target, map, &rect, false))
            return Fail(DX12W3DSurfaces::LastError());
        for (int y = 0; y < rect.bottom - rect.top; ++y)
            std::memcpy(static_cast<std::uint8_t *>(map.data) +
                        static_cast<std::size_t>(y) * map.rowPitch,
                        pixels.data() + static_cast<std::size_t>(y) *
                        (rect.right - rect.left) * 4u,
                        (rect.right - rect.left) * 4u);
        if (!DX12W3DSurfaces::Unlock(target))
            return Fail(DX12W3DSurfaces::LastError());
        return true;
    }

    bool Snapshot(DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
                  RenderSurfaceHandle source, const RenderSurfaceRect & region,
                  std::vector<std::uint8_t> & pixels)
    {
        if (targets.Owns(source))
            return Read(frame, targets, source, region, pixels);
        RenderSurfaceMap map;
        if (!DX12W3DSurfaces::Lock(source, map, &region, true))
            return Fail(DX12W3DSurfaces::LastError());
        const auto width = static_cast<unsigned>(region.right - region.left);
        const auto height = static_cast<unsigned>(region.bottom - region.top);
        pixels.resize(static_cast<std::size_t>(width) * height * 4u);
        for (unsigned y = 0; y < height; ++y)
            std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width * 4u,
                        static_cast<const std::uint8_t *>(map.data) +
                            static_cast<std::size_t>(y) * map.rowPitch, width * 4u);
        if (!DX12W3DSurfaces::Unlock(source))
            return Fail(DX12W3DSurfaces::LastError());
        return true;
    }
};

DX12W3DSurfaceTransfers::DX12W3DSurfaceTransfers() : impl_(new Impl) {}
DX12W3DSurfaceTransfers::~DX12W3DSurfaceTransfers() = default;
void DX12W3DSurfaceTransfers::Reset() { impl_->mapped.clear(); impl_->error.clear(); }
const char * DX12W3DSurfaceTransfers::LastError() const { return impl_->error.c_str(); }

unsigned DX12W3DSurfaceTransfers::ReadRegion(
    DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
    RenderSurfaceHandle surface, unsigned char * destination, unsigned capacity,
    const RenderSurfaceRect & region)
{
    impl_->error.clear();
    const std::uint64_t bytes =
        region.right > region.left && region.bottom > region.top ?
        static_cast<std::uint64_t>(region.right - region.left) *
            (region.bottom - region.top) * 4ull : 0ull;
    if (!destination || !bytes || bytes > capacity || bytes > MaxTransferBytes)
    {
        impl_->Fail("R08 color readback destination is undersized or invalid");
        return 0;
    }
    std::vector<std::uint8_t> pixels;
    if (!impl_->Read(frame, targets, surface, region, pixels))
        return 0;
    std::memcpy(destination, pixels.data(), pixels.size());
    return static_cast<unsigned>(pixels.size());
}

bool DX12W3DSurfaceTransfers::Lock(
    DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
    RenderSurfaceHandle surface, RenderSurfaceMap & result,
    const RenderSurfaceRect * rect, bool readOnly)
{
    result = RenderSurfaceMap();
    impl_->error.clear();
    if (impl_->mapped.count(surface))
        return impl_->Fail("Nested R08 GPU surface map rejected");
    RenderSurfaceDescription desc;
    if (!targets.Owns(surface) || !impl_->SurfaceInfo(targets, surface, desc))
        return impl_->Fail("Only R07 GPU color surfaces use R08 mapped readback");
    const auto area = rect ? *rect : Full(desc);
    if (!ValidRect(area, desc.width, desc.height))
        return impl_->Fail("Invalid R08 GPU surface map area");
    Impl::Mapping mapping;
    mapping.region = area;
    mapping.readOnly = readOnly;
    if (!impl_->Read(frame, targets, surface, area, mapping.pixels))
        return false;
    auto it = impl_->mapped.emplace(surface, std::move(mapping)).first;
    result.data = it->second.pixels.data();
    result.rowPitch = (area.right - area.left) * 4;
    return true;
}

bool DX12W3DSurfaceTransfers::Unlock(
    DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
    RenderSurfaceHandle surface)
{
    impl_->error.clear();
    auto found = impl_->mapped.find(surface);
    if (found == impl_->mapped.end())
        return impl_->Fail("R08 GPU surface unlock without map");
    const auto area = found->second.region;
    const bool done = found->second.readOnly ||
        impl_->Write(frame, targets, surface, area,
                     found->second.pixels.data(), (area.right - area.left) * 4);
    impl_->mapped.erase(found);
    return done;
}

bool DX12W3DSurfaceTransfers::Copy(
    DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
    RenderSurfaceHandle source, const RenderSurfaceRect * rect,
    RenderSurfaceHandle destination, const RenderSurfacePoint * point)
{
    impl_->error.clear();
    RenderSurfaceDescription a, b;
    if (!impl_->SurfaceInfo(targets, source, a) ||
        !impl_->SurfaceInfo(targets, destination, b))
        return false;
    const auto region = rect ? *rect : Full(a);
    const auto origin = point ? *point : RenderSurfacePoint{0,0};
    const auto width = region.right - region.left;
    const auto height = region.bottom - region.top;
    if (!ValidRect(region, a.width, a.height) ||
        origin.x < 0 || origin.y < 0 ||
        static_cast<std::uint64_t>(origin.x) + width > b.width ||
        static_cast<std::uint64_t>(origin.y) + height > b.height ||
        (a.pixelFormat != 2 && a.pixelFormat != 3) ||
        (b.pixelFormat != 2 && b.pixelFormat != 3))
        return impl_->Fail("R08 copy requires in-bounds 32-bit color surfaces");
    const RenderSurfaceRect dst = {origin.x, origin.y,
                                  origin.x + width, origin.y + height};
    if (!targets.Owns(source) && !targets.Owns(destination))
        return DX12W3DSurfaces::Copy(source, rect, destination, point) ||
               impl_->Fail(DX12W3DSurfaces::LastError());

    // GPU->GPU fast-path: native one-sample CopyTextureRegion when formats
    // match and resources do not alias. Other combinations use a validated
    // CPU snapshot so RGBA/BGRA swizzle and overlapping copies stay correct.
    if (targets.Owns(source) && targets.Owns(destination) && source != destination)
    {
        ID3D12Resource * nativeA = nullptr, * nativeB = nullptr;
        if (!targets.TransferResource(frame, source, false, nativeA) ||
            !targets.TransferResource(frame, destination, true, nativeB))
            return impl_->Fail(targets.LastError());
        if (nativeA != nativeB &&
            nativeA->GetDesc().Format == nativeB->GetDesc().Format)
        {
            if (!targets.TransitionTransfer(frame, source, D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                !targets.TransitionTransfer(frame, destination, D3D12_RESOURCE_STATE_COPY_DEST))
                return impl_->Fail(targets.LastError());
            const auto src = Image(nativeA), dstLoc = Image(nativeB);
            const D3D12_BOX box = {
                static_cast<UINT>(region.left), static_cast<UINT>(region.top), 0,
                static_cast<UINT>(region.right), static_cast<UINT>(region.bottom), 1
            };
            frame.ActiveCommandList()->CopyTextureRegion(
                &dstLoc, static_cast<UINT>(origin.x), static_cast<UINT>(origin.y),
                0, &src, &box);
            return impl_->Restore(frame, targets, source, nativeA) &&
                impl_->Restore(frame, targets, destination, nativeB);
        }
    }
    std::vector<std::uint8_t> pixels;
    if (!impl_->Snapshot(frame, targets, source, region, pixels))
        return false;
    if (targets.Owns(destination))
        return impl_->Write(frame, targets, destination, dst, pixels.data(), width * 4);
    return impl_->FillCPU(destination, dst, pixels);
}

bool DX12W3DSurfaceTransfers::Blit(
    DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
    RenderSurfaceHandle source, const RenderSurfaceRect & sourceRect,
    RenderSurfaceHandle destination, const RenderSurfaceRect & destinationRect,
    RenderSurfaceBlitFilter filter)
{
    impl_->error.clear();
    RenderSurfaceDescription a, b;
    if (!impl_->SurfaceInfo(targets, source, a) ||
        !impl_->SurfaceInfo(targets, destination, b))
        return false;
    if (!ValidRect(sourceRect, a.width, a.height) ||
        !ValidRect(destinationRect, b.width, b.height) ||
        (a.pixelFormat != 2 && a.pixelFormat != 3) ||
        (b.pixelFormat != 2 && b.pixelFormat != 3))
        return impl_->Fail("R08 blit requires in-bounds 32-bit W3D color surfaces");
    if (!targets.Owns(source) && !targets.Owns(destination))
        return DX12W3DSurfaces::Blit(source, sourceRect, destination, destinationRect, filter) ||
            impl_->Fail(DX12W3DSurfaces::LastError());
    const auto sw = sourceRect.right - sourceRect.left;
    const auto sh = sourceRect.bottom - sourceRect.top;
    const auto dw = destinationRect.right - destinationRect.left;
    const auto dh = destinationRect.bottom - destinationRect.top;
    RenderSurfaceDescription tempDesc;
    tempDesc.width = static_cast<unsigned>(sw);
    tempDesc.height = static_cast<unsigned>(sh);
    tempDesc.pixelFormat = 2;
    const auto in = DX12W3DSurfaces::Create(tempDesc);
    if (!in) return impl_->Fail(DX12W3DSurfaces::LastError());
    tempDesc.width = static_cast<unsigned>(dw);
    tempDesc.height = static_cast<unsigned>(dh);
    const auto out = DX12W3DSurfaces::Create(tempDesc);
    if (!out) { DX12W3DSurfaces::Release(in); return impl_->Fail(DX12W3DSurfaces::LastError()); }
    std::vector<std::uint8_t> pixels;
    bool ok = impl_->Snapshot(frame, targets, source, sourceRect, pixels);
    const RenderSurfaceRect sr = {0, 0, sw, sh};
    const RenderSurfaceRect dr = {0, 0, dw, dh};
    if (ok) ok = impl_->FillCPU(in, sr, pixels);
    if (ok) ok = DX12W3DSurfaces::Blit(in, sr, out, dr, filter);
    if (ok)
    {
        ok = impl_->Snapshot(frame, targets, out, dr, pixels);
        if (ok)
            ok = targets.Owns(destination) ?
                impl_->Write(frame, targets, destination, destinationRect,
                             pixels.data(), dw * 4) :
                impl_->FillCPU(destination, destinationRect, pixels);
    }
    DX12W3DSurfaces::Release(in);
    DX12W3DSurfaces::Release(out);
    return ok || impl_->Fail("R08 staged stretch blit failed");
}

RenderTexture2DHandle DX12W3DSurfaceTransfers::ToTexture(
    DX12PresentationDevice & frame, DX12W3DRenderTargets & targets,
    RenderSurfaceHandle source, unsigned mips)
{
    impl_->error.clear();
    RenderSurfaceDescription desc;
    if (!targets.Owns(source) || !impl_->SurfaceInfo(targets, source, desc) ||
        desc.pixelFormat != 2)
    {
        impl_->Fail("GPU-to-texture R08 conversion needs BGRA8 surface");
        return nullptr;
    }
    const auto region = Full(desc);
    std::vector<std::uint8_t> pixels;
    if (!impl_->Read(frame, targets, source, region, pixels))
        return nullptr;
    const auto cpu = DX12W3DSurfaces::Create(desc);
    if (!cpu || !impl_->FillCPU(cpu, region, pixels))
    {
        DX12W3DSurfaces::Release(cpu);
        return nullptr;
    }
    const auto texture = DX12W3DSurfaces::ToTexture(frame.NativeDevice(), cpu, mips);
    DX12W3DSurfaces::Release(cpu);
    return texture;
}
