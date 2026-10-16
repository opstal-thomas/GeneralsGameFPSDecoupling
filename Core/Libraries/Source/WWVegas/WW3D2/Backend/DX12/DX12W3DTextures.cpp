/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#include "DX12W3DTextures.h"
#include "DX12PresentationDevice.h"

#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    thread_local std::string lastError;
    std::atomic<std::uint64_t> nextTextureIdentity{1};

    bool Fail(const char * text)
    {
        lastError = text;
        return false;
    }

    // Same ordering as WW3D2/ww3dformat.h; source-contract tests enforce it.
    enum W3D : unsigned
    {
        Unknown, R8G8B8, A8R8G8B8, X8R8G8B8, R5G6B5, X1R5G5B5,
        A1R5G5B5, A4R4G4B4, R3G3B2, A8, A8R3G3B2, X4R4G4B4,
        A8P8, P8, L8, A8L8, A4L4, U8V8, L6V5U5, X8L8V8U8,
        DXT1, DXT2, DXT3, DXT4, DXT5, FormatCount
    };

    enum class Kind { Texture2D, Cube, Volume };

    bool FormatInfo(unsigned format, DXGI_FORMAT & dxgi,
                    unsigned & cpuBytes, unsigned & gpuBlockBytes, bool & compressed)
    {
        compressed = false;
        cpuBytes = gpuBlockBytes = 0;
        dxgi = DXGI_FORMAT_UNKNOWN;
        switch (format)
        {
            case A8R8G8B8:
            case X8R8G8B8:
            case R8G8B8:
            case R5G6B5:
            case X1R5G5B5:
            case A1R5G5B5:
            case A4R4G4B4:
            case R3G3B2:
            case A8:
            case A8R3G3B2:
            case X4R4G4B4:
            case L8:
            case A8L8:
            case A4L4:
            case U8V8:
            case L6V5U5:
            case X8L8V8U8:
                dxgi = DXGI_FORMAT_B8G8R8A8_UNORM;
                gpuBlockBytes = 4;
                break;
            case DXT1:
                dxgi = DXGI_FORMAT_BC1_UNORM;
                gpuBlockBytes = 8;
                compressed = true;
                break;
            case DXT2:
            case DXT3:
                dxgi = DXGI_FORMAT_BC2_UNORM;
                gpuBlockBytes = 16;
                compressed = true;
                break;
            case DXT4:
            case DXT5:
                dxgi = DXGI_FORMAT_BC3_UNORM;
                gpuBlockBytes = 16;
                compressed = true;
                break;
            default:
                return false; // Indexed P8/A8P8 require palette conversion before creation.
        }
        if (compressed)
            return true;
        switch (format)
        {
            case R8G8B8: cpuBytes = 3; break;
            case A8R8G8B8:
            case X8R8G8B8:
            case X8L8V8U8: cpuBytes = 4; break;
            case R5G6B5:
            case X1R5G5B5:
            case A1R5G5B5:
            case A4R4G4B4:
            case A8R3G3B2:
            case A8L8:
            case U8V8:
            case L6V5U5: cpuBytes = 2; break;
            default: cpuBytes = 1; break;
        }
        return true;
    }

    unsigned FullMipCount(unsigned width, unsigned height, unsigned depth)
    {
        unsigned count = 1;
        while (width > 1 || height > 1 || depth > 1)
        {
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
            depth = std::max(1u, depth / 2);
            ++count;
        }
        return count;
    }

    unsigned Expand(unsigned c, unsigned bits)
    {
        const unsigned maxValue = (1u << bits) - 1u;
        return (c * 255u + maxValue / 2u) / maxValue;
    }

    void ConvertPixel(unsigned format, const std::uint8_t * in, std::uint8_t * out)
    {
        unsigned b = 0, g = 0, r = 0, a = 255;
        // Single-byte W3D formats must not read beyond their last pixel.
        const unsigned short v = static_cast<unsigned short>(
            in[0] | ((format == R3G3B2 || format == A8 ||
                       format == L8 || format == A4L4) ? 0u :
                      static_cast<unsigned short>(in[1]) << 8));
        switch (format)
        {
            case A8R8G8B8: b = in[0]; g = in[1]; r = in[2]; a = in[3]; break;
            case X8R8G8B8: b = in[0]; g = in[1]; r = in[2]; break;
            case R8G8B8: b = in[0]; g = in[1]; r = in[2]; break;
            case R5G6B5:
                b = Expand(v & 31, 5);
                g = Expand((v >> 5) & 63, 6);
                r = Expand((v >> 11) & 31, 5); break;
            case X1R5G5B5:
            case A1R5G5B5:
                b = Expand(v & 31, 5);
                g = Expand((v >> 5) & 31, 5);
                r = Expand((v >> 10) & 31, 5);
                a = format == X1R5G5B5 || (v & 0x8000) ? 255 : 0; break;
            case A4R4G4B4:
            case X4R4G4B4:
                b = Expand(v & 15, 4);
                g = Expand((v >> 4) & 15, 4);
                r = Expand((v >> 8) & 15, 4);
                a = format == X4R4G4B4 ? 255 : Expand((v >> 12) & 15, 4); break;
            case R3G3B2:
                b = Expand(in[0] & 3, 2);
                g = Expand((in[0] >> 2) & 7, 3);
                r = Expand((in[0] >> 5) & 7, 3); break;
            case A8R3G3B2:
                b = Expand(in[0] & 3, 2);
                g = Expand((in[0] >> 2) & 7, 3);
                r = Expand((in[0] >> 5) & 7, 3);
                a = in[1]; break;
            case A8: b = g = r = 255; a = in[0]; break;
            case L8: b = g = r = in[0]; break;
            case A8L8: b = g = r = in[0]; a = in[1]; break;
            case A4L4:
                b = g = r = Expand(in[0] & 15, 4);
                a = Expand((in[0] >> 4) & 15, 4); break;
            case U8V8:
                b = 128; g = in[1]; r = in[0]; break;
            case L6V5U5:
                b = Expand((v >> 10) & 63, 6);
                g = Expand((v >> 5) & 31, 5);
                r = Expand(v & 31, 5); break;
            case X8L8V8U8:
                b = in[2]; g = in[1]; r = in[0]; break;
            default: break;
        }
        out[0] = static_cast<std::uint8_t>(b);
        out[1] = static_cast<std::uint8_t>(g);
        out[2] = static_cast<std::uint8_t>(r);
        out[3] = static_cast<std::uint8_t>(a);
    }

    D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES p = {};
        p.Type = type;
        p.CreationNodeMask = p.VisibleNodeMask = 1;
        return p;
    }

    D3D12_RESOURCE_DESC UploadBuffer(UINT64 bytes)
    {
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = bytes;
        d.Height = 1;
        d.DepthOrArraySize = d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return d;
    }

    struct Level
    {
        unsigned width = 0, height = 0, depth = 0;
        std::size_t rowPitch = 0, slicePitch = 0;
        bool mapped = false;
        std::vector<std::uint8_t> bytes;
    };

    struct NativeTexture
    {
        std::atomic<unsigned> refs{1};
        std::mutex mutex;
        std::uint64_t identity = 0;
        std::uint64_t contentRevision = 1;
        std::uint64_t gpuGeneration = 0;
        Kind kind = Kind::Texture2D;
        unsigned width = 0, height = 0, depth = 1, mipLevels = 0;
        unsigned format = 0, cpuBytes = 0, gpuBlockBytes = 0;
        unsigned priority = 0;
        bool compressed = false;
        bool dirty = true;
        DXGI_FORMAT dxgi = DXGI_FORMAT_UNKNOWN;
        ComPtr<ID3D12Device> owner;
        ComPtr<ID3D12Resource> gpu;
        std::vector<Level> levels;

        unsigned Subresource(unsigned face, unsigned mip) const
        {
            return face * mipLevels + mip; // D3D12 cube array slice order
        }

        bool Describe(unsigned mip, RenderTextureLevelDescription & result) const
        {
            result = RenderTextureLevelDescription();
            if (mip >= mipLevels)
                return Fail("Texture level out of bounds");
            const Level & l = levels[mip];
            result.width = l.width;
            result.height = l.height;
            result.depth = l.depth;
            result.pixelFormat = format;
            return true;
        }

        bool Map(unsigned face, unsigned mip, RenderSurfaceMap & out, RenderVolumeMap * volume)
        {
            out = RenderSurfaceMap();
            if (volume) *volume = RenderVolumeMap();
            const unsigned faces = kind == Kind::Cube ? 6u : 1u;
            if (face >= faces || mip >= mipLevels)
                return Fail("Texture face/mip out of bounds");
            Level & l = levels[Subresource(face, mip)];
            if (l.mapped || l.rowPitch > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                l.slicePitch > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return Fail("Nested texture lock or pitch overflow");
            l.mapped = true;
            out.data = l.bytes.data();
            out.rowPitch = static_cast<int>(l.rowPitch);
            if (volume)
            {
                volume->data = out.data;
                volume->rowPitch = out.rowPitch;
                volume->slicePitch = static_cast<int>(l.slicePitch);
            }
            return true;
        }

        bool Unmap(unsigned face, unsigned mip)
        {
            const unsigned faces = kind == Kind::Cube ? 6u : 1u;
            if (face >= faces || mip >= mipLevels)
                return Fail("Texture face/mip out of bounds");
            Level & l = levels[Subresource(face, mip)];
            if (!l.mapped)
                return Fail("Unmap without a locked texture mip");
            l.mapped = false;
            dirty = true;
            ++contentRevision;
            return true;
        }

        bool Upload(DX12PresentationDevice & frame)
        {
            ID3D12Device * device = frame.NativeDevice();
            ID3D12GraphicsCommandList * cmd = frame.ActiveCommandList();
            if (!device || !cmd)
                return Fail("Texture upload requires an active native DX12 frame");
            for (const Level & l : levels)
                if (l.mapped)
                    return Fail("Cannot upload texture with locked mip levels");

            // Recreate after device reset: CPU shadows are authoritative.
            if (owner.Get() != device)
            {
                owner = device;
                gpu.Reset();
                dirty = true;
            }
            const D3D12_RESOURCE_STATES sampled =
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            if (!gpu || dirty)
            {
                D3D12_RESOURCE_DESC desc = {};
                desc.Dimension = kind == Kind::Volume
                    ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Width = width;
                desc.Height = height;
                desc.DepthOrArraySize = static_cast<UINT16>(
                    kind == Kind::Cube ? 6u : (kind == Kind::Volume ? depth : 1u));
                desc.MipLevels = static_cast<UINT16>(mipLevels);
                desc.Format = dxgi;
                desc.SampleDesc.Count = 1;
                desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                if (desc.Width == 0)
                    return Fail("Invalid texture descriptor");

                ComPtr<ID3D12Resource> next;
                const D3D12_HEAP_PROPERTIES defaultHeap = Heap(D3D12_HEAP_TYPE_DEFAULT);
                if (FAILED(device->CreateCommittedResource(
                        &defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                        IID_PPV_ARGS(&next))))
                    return Fail("Creating GPU-local mipmapped DX12 texture failed");

                const UINT count = static_cast<UINT>(levels.size());
                std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(count);
                std::vector<UINT> rowCount(count);
                std::vector<UINT64> rowBytes(count);
                UINT64 totalBytes = 0;
                device->GetCopyableFootprints(&desc, 0, count, 0, layouts.data(),
                                              rowCount.data(), rowBytes.data(), &totalBytes);
                if (!totalBytes || totalBytes > std::numeric_limits<SIZE_T>::max() ||
                    totalBytes > 512ull * 1024 * 1024)
                    return Fail("Invalid or excessive D3D12 texture upload footprint");

                ComPtr<ID3D12Resource> staging;
                const auto uploadHeap = Heap(D3D12_HEAP_TYPE_UPLOAD);
                const auto bufferDesc = UploadBuffer(totalBytes);
                if (FAILED(device->CreateCommittedResource(
                        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                        IID_PPV_ARGS(&staging))))
                    return Fail("Creating DX12 mip upload staging buffer failed");

                std::uint8_t * mapped = nullptr;
                const D3D12_RANGE none = {0, 0};
                if (FAILED(staging->Map(0, &none, reinterpret_cast<void **>(&mapped))) ||
                    !mapped)
                    return Fail("Mapping DX12 mip upload staging failed");

                bool copyValid = true;
                for (unsigned i = 0; i < count; ++i)
                {
                    const Level & l = levels[i];
                    const auto & fp = layouts[i];
                    const std::size_t gpuPitch = compressed
                        ? std::max(1u, (l.width + 3u) / 4u) * gpuBlockBytes
                        : static_cast<std::size_t>(l.width) * 4u;
                    const unsigned rows = compressed
                        ? std::max(1u, (l.height + 3u) / 4u) : l.height;
                    if (rowCount[i] != rows || rowBytes[i] != gpuPitch ||
                        fp.Footprint.Depth != l.depth || fp.Footprint.RowPitch < gpuPitch)
                    {
                        copyValid = false;
                        break;
                    }
                    for (unsigned z = 0; z < l.depth; ++z)
                    {
                        for (unsigned y = 0; y < rows; ++y)
                        {
                            const auto * from = l.bytes.data() + z * l.slicePitch +
                                                 y * l.rowPitch;
                            auto * to = mapped + fp.Offset +
                                (static_cast<std::size_t>(z) * rows + y) *
                                fp.Footprint.RowPitch;
                            if (compressed || format == A8R8G8B8)
                                std::memcpy(to, from, gpuPitch);
                            else
                                for (unsigned x = 0; x < l.width; ++x)
                                    ConvertPixel(format, from + x * cpuBytes, to + x * 4u);
                        }
                    }
                }
                const D3D12_RANGE written = {0, static_cast<SIZE_T>(totalBytes)};
                staging->Unmap(0, &written);
                if (!copyValid)
                    return Fail("DX12 texture footprint does not match W3D mip layout");

                if (!frame.RegisterFrameResource(next.Get(), D3D12_RESOURCE_STATE_COPY_DEST) ||
                    !frame.RetainFrameResource(next.Get()) ||
                    !frame.RetainFrameResource(staging.Get()))
                    return Fail(frame.LastError());
                for (UINT i = 0; i < count; ++i)
                {
                    D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
                    src.pResource = staging.Get();
                    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    src.PlacedFootprint = layouts[i];
                    dst.pResource = next.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    dst.SubresourceIndex = i;
                    cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                }
                if (!frame.TransitionFrameResource(next.Get(), sampled))
                    return Fail(frame.LastError());

                gpu = std::move(next);
                dirty = false;
                ++gpuGeneration;
            }

            D3D12_RESOURCE_STATES tracked = D3D12_RESOURCE_STATE_COMMON;
            if (!frame.GetFrameResourceState(gpu.Get(), tracked))
            {
                if (!frame.RegisterFrameResource(gpu.Get(), sampled))
                    return Fail(frame.LastError());
            }
            else if (tracked != sampled &&
                     !frame.TransitionFrameResource(gpu.Get(), sampled))
                return Fail(frame.LastError());
            return frame.RetainFrameResource(gpu.Get());
        }
    };

    NativeTexture * Object(RenderTextureHandle p)
    {
        return reinterpret_cast<NativeTexture *>(p);
    }
    RenderTextureHandle Handle(NativeTexture * p)
    {
        return reinterpret_cast<RenderTextureHandle>(p);
    }

    NativeTexture * Create(ID3D12Device * device, Kind kind,
                           unsigned width, unsigned height, unsigned depth,
                           unsigned format, unsigned requestedMips)
    {
        DXGI_FORMAT dxgi = DXGI_FORMAT_UNKNOWN;
        unsigned cpuBytes = 0, gpuBlockBytes = 0;
        bool compressed = false;
        if (!device || !width || !height || !depth || width > 16384 || height > 16384 ||
            depth > 2048 || !FormatInfo(format, dxgi, cpuBytes, gpuBlockBytes, compressed) ||
            (kind == Kind::Volume && compressed) ||
            (kind == Kind::Cube && (width != height || depth != 1)))
        {
            Fail("Unsupported or invalid W3D texture dimensions/format");
            return nullptr;
        }
        const unsigned maxMip = FullMipCount(width, height, kind == Kind::Volume ? depth : 1u);
        const unsigned mips = requestedMips ? requestedMips : maxMip;
        if (mips > maxMip || mips > 15)
        {
            Fail("Invalid number of W3D mipmap levels");
            return nullptr;
        }
        auto * result = new (std::nothrow) NativeTexture;
        if (!result)
        {
            Fail("Allocating backend texture record failed");
            return nullptr;
        }
        result->identity = nextTextureIdentity.fetch_add(1, std::memory_order_relaxed);
        result->owner = device;
        result->kind = kind;
        result->format = format;
        result->dxgi = dxgi;
        result->width = width;
        result->height = height;
        result->depth = depth;
        result->mipLevels = mips;
        result->cpuBytes = cpuBytes;
        result->gpuBlockBytes = gpuBlockBytes;
        result->compressed = compressed;
        result->levels.reserve(mips * (kind == Kind::Cube ? 6u : 1u));

        std::size_t total = 0;
        const unsigned faceCount = kind == Kind::Cube ? 6u : 1u;
        for (unsigned face = 0; face < faceCount; ++face)
        {
            unsigned w = width, h = height, d = depth;
            for (unsigned mip = 0; mip < mips; ++mip)
            {
                Level level;
                level.width = w; level.height = h; level.depth = d;
                const unsigned rows = compressed ? std::max(1u, (h + 3) / 4) : h;
                level.rowPitch = compressed
                    ? static_cast<std::size_t>(std::max(1u, (w + 3) / 4)) * gpuBlockBytes
                    : static_cast<std::size_t>(w) * cpuBytes;
                level.slicePitch = level.rowPitch * rows;
                const std::size_t bytes = level.slicePitch * d;
                total += bytes;
                if (total > 512ull * 1024 * 1024 || bytes > 512ull * 1024 * 1024)
                {
                    delete result;
                    Fail("W3D texture CPU shadow exceeds R04 budget");
                    return nullptr;
                }
                level.bytes.resize(bytes, 0);
                result->levels.push_back(std::move(level));
                w = std::max(1u, w / 2); h = std::max(1u, h / 2);
                d = kind == Kind::Volume ? std::max(1u, d / 2) : 1u;
            }
        }
        return result;
    }
}

namespace DX12W3DTextures
{
    RenderTexture2DHandle Create2D(ID3D12Device * device,
                                   const RenderTexture2DDescription & desc)
    {
        lastError.clear();
        if (desc.renderTarget)
        {
            Fail("Render-to-texture attachments require the native R07 target manager");
            return nullptr;
        }
        return reinterpret_cast<RenderTexture2DHandle>(
            Create(device, Kind::Texture2D, desc.width, desc.height, 1,
                   desc.pixelFormat, desc.mipLevels));
    }
    RenderTextureCubeHandle CreateCube(ID3D12Device * device,
                                       const RenderTextureCubeDescription & desc)
    {
        lastError.clear();
        if (desc.renderTarget)
        {
            Fail("Cube render targets are not implemented before R07");
            return nullptr;
        }
        return reinterpret_cast<RenderTextureCubeHandle>(
            Create(device, Kind::Cube, desc.edgeLength, desc.edgeLength, 1,
                   desc.pixelFormat, desc.mipLevels));
    }
    RenderTextureVolumeHandle CreateVolume(ID3D12Device * device,
                                           const RenderTextureVolumeDescription & desc)
    {
        lastError.clear();
        return reinterpret_cast<RenderTextureVolumeHandle>(
            Create(device, Kind::Volume, desc.width, desc.height, desc.depth,
                   desc.pixelFormat, desc.mipLevels));
    }

    bool ConvertRowToBGRA8(unsigned format, const std::uint8_t * source,
                           unsigned pixels, std::uint8_t * destination)
    {
        lastError.clear();
        DXGI_FORMAT native = DXGI_FORMAT_UNKNOWN;
        unsigned cpuBytes = 0, blockBytes = 0;
        bool compressed = false;
        if (!source || !destination ||
            !FormatInfo(format, native, cpuBytes, blockBytes, compressed) ||
            compressed || !cpuBytes)
            return Fail("CPU row conversion requires uncompressed W3D pixels");
        for (unsigned x = 0; x < pixels; ++x)
            ConvertPixel(format, source + static_cast<std::size_t>(x) * cpuBytes,
                         destination + static_cast<std::size_t>(x) * 4u);
        return true;
    }

    bool SupportsFormat(unsigned format)
    {
        DXGI_FORMAT native = DXGI_FORMAT_UNKNOWN;
        unsigned cpuBytes = 0, blockBytes = 0;
        bool compressed = false;
        return FormatInfo(format, native, cpuBytes, blockBytes, compressed);
    }

    unsigned MipCount(RenderTextureHandle handle)
    {
        return handle ? Object(handle)->mipLevels : 0;
    }
    unsigned MemoryUsage(RenderTextureHandle handle)
    {
        if (!handle) return 0;
        NativeTexture & t = *Object(handle);
        std::lock_guard<std::mutex> lock(t.mutex);
        std::size_t total = 0;
        for (const auto & level : t.levels) total += level.bytes.size();
        return static_cast<unsigned>(std::min<std::size_t>(total, UINT32_MAX));
    }
    bool Describe(RenderTextureHandle handle, unsigned mip,
                  RenderTextureLevelDescription & desc)
    {
        lastError.clear();
        desc = RenderTextureLevelDescription();
        if (!handle) return Fail("Cannot describe null W3D texture");
        NativeTexture & t = *Object(handle);
        std::lock_guard<std::mutex> lock(t.mutex);
        return t.Describe(mip, desc);
    }

    bool Map2D(RenderTexture2DHandle h, unsigned mip, RenderSurfaceMap & out)
    {
        lastError.clear();
        out = RenderSurfaceMap();
        if (!h) return Fail("Null 2D texture map");
        NativeTexture & t = *Object(reinterpret_cast<RenderTextureHandle>(h));
        std::lock_guard<std::mutex> lock(t.mutex);
        return t.kind == Kind::Texture2D && t.Map(0, mip, out, nullptr);
    }
    bool Unmap2D(RenderTexture2DHandle h, unsigned mip)
    {
        lastError.clear();
        if (!h) return Fail("Null 2D texture unmap");
        NativeTexture & t = *Object(reinterpret_cast<RenderTextureHandle>(h));
        std::lock_guard<std::mutex> lock(t.mutex);
        return t.kind == Kind::Texture2D && t.Unmap(0, mip);
    }
    bool MapCube(RenderTextureCubeHandle h, unsigned face, unsigned mip,
                 RenderSurfaceMap & out)
    {
        lastError.clear();
        out = RenderSurfaceMap();
        if (!h) return Fail("Null cube texture map");
        NativeTexture & t = *Object(reinterpret_cast<RenderTextureHandle>(h));
        std::lock_guard<std::mutex> lock(t.mutex);
        return t.kind == Kind::Cube && t.Map(face, mip, out, nullptr);
    }
    bool UnmapCube(RenderTextureCubeHandle h, unsigned face, unsigned mip)
    {
        lastError.clear();
        if (!h) return Fail("Null cube texture unmap");
        NativeTexture & t = *Object(reinterpret_cast<RenderTextureHandle>(h));
        std::lock_guard<std::mutex> lock(t.mutex);
        return t.kind == Kind::Cube && t.Unmap(face, mip);
    }
    bool MapVolume(RenderTextureVolumeHandle h, unsigned mip, RenderVolumeMap & out)
    {
        lastError.clear();
        out = RenderVolumeMap();
        if (!h) return Fail("Null volume texture map");
        NativeTexture & t = *Object(reinterpret_cast<RenderTextureHandle>(h));
        std::lock_guard<std::mutex> lock(t.mutex);
        RenderSurfaceMap unused;
        return t.kind == Kind::Volume && t.Map(0, mip, unused, &out);
    }
    bool UnmapVolume(RenderTextureVolumeHandle h, unsigned mip)
    {
        lastError.clear();
        if (!h) return Fail("Null volume texture unmap");
        NativeTexture & t = *Object(reinterpret_cast<RenderTextureHandle>(h));
        std::lock_guard<std::mutex> lock(t.mutex);
        return t.kind == Kind::Volume && t.Unmap(0, mip);
    }

    bool Update(RenderTextureHandle source, RenderTextureHandle target)
    {
        lastError.clear();
        if (!source || !target || source == target)
            return source && source == target ? true : Fail("UpdateTexture needs valid handles");
        NativeTexture * src = Object(source), * dst = Object(target);
        std::scoped_lock guard(src->mutex, dst->mutex);
        if (src->kind != dst->kind || src->width != dst->width ||
            src->height != dst->height || src->depth != dst->depth ||
            src->format != dst->format || src->levels.size() != dst->levels.size())
            return Fail("Texture update descriptors are incompatible");
        for (std::size_t i = 0; i < src->levels.size(); ++i)
            if (src->levels[i].mapped || dst->levels[i].mapped)
                return Fail("Texture update cannot overlap mapped subresources");
        for (std::size_t i = 0; i < src->levels.size(); ++i)
            dst->levels[i].bytes = src->levels[i].bytes;
        dst->dirty = true;
        ++dst->contentRevision;
        return true;
    }

    bool Snapshot(RenderTextureHandle h, ViewSnapshot & view)
    {
        lastError.clear();
        view = ViewSnapshot();
        if (!h) return Fail("Cannot snapshot null DX12 texture");
        NativeTexture & t = *Object(h);
        std::lock_guard<std::mutex> lock(t.mutex);
        view.resource = t.gpu;
        view.identity = t.identity;
        view.contentRevision = t.contentRevision;
        view.gpuGeneration = t.gpuGeneration;
        view.dirty = t.dirty;
        view.descriptor.Format = t.dxgi;
        view.descriptor.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        switch (t.kind)
        {
            case Kind::Texture2D:
                view.descriptor.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.descriptor.Texture2D.MipLevels = t.mipLevels;
                break;
            case Kind::Cube:
                view.descriptor.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                view.descriptor.TextureCube.MipLevels = t.mipLevels;
                break;
            case Kind::Volume:
                view.descriptor.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                view.descriptor.Texture3D.MipLevels = t.mipLevels;
                break;
        }
        return true;
    }

    bool Prepare(DX12PresentationDevice & frame, RenderTextureHandle h)
    {
        lastError.clear();
        if (!h) return Fail("Cannot prepare null DX12 texture");
        NativeTexture & t = *Object(h);
        std::lock_guard<std::mutex> lock(t.mutex);
        return t.Upload(frame);
    }
    ID3D12Resource * NativeResource(RenderTextureHandle h)
    {
        if (!h) return nullptr;
        NativeTexture & t = *Object(h);
        std::lock_guard<std::mutex> lock(t.mutex);
        return t.gpu.Get(); // borrowed, valid while the handle is retained
    }
    DXGI_FORMAT NativeFormat(RenderTextureHandle h)
    {
        return h ? Object(h)->dxgi : DXGI_FORMAT_UNKNOWN;
    }

    void Retain(RenderTextureHandle h)
    {
        if (h) Object(h)->refs.fetch_add(1, std::memory_order_relaxed);
    }
    void Release(RenderTextureHandle h)
    {
        if (h && Object(h)->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete Object(h);
    }
    unsigned GetPriority(RenderTextureHandle h)
    {
        if (!h) return 0;
        NativeTexture & t = *Object(h);
        std::lock_guard<std::mutex> lock(t.mutex);
        return t.priority;
    }
    unsigned SetPriority(RenderTextureHandle h, unsigned priority)
    {
        if (!h) return 0;
        NativeTexture & t = *Object(h);
        std::lock_guard<std::mutex> lock(t.mutex);
        const unsigned prior = t.priority;
        t.priority = priority;
        return prior;
    }

    bool ConvertPaletteToBGRA8(const std::uint8_t * src, unsigned w, unsigned h,
                               unsigned sourcePitch, bool alphaIndexed,
                               const std::uint32_t * palette, std::uint8_t * dst,
                               unsigned dstPitch)
    {
        lastError.clear();
        if (!src || !dst || !palette || !w || !h ||
            sourcePitch < static_cast<std::size_t>(w) * (alphaIndexed ? 2u : 1u) ||
            dstPitch < static_cast<std::size_t>(w) * 4u)
            return Fail("Invalid indexed W3D image/palette dimensions");
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x)
            {
                const std::size_t off = static_cast<std::size_t>(x) *
                                        (alphaIndexed ? 2u : 1u);
                const unsigned index = src[static_cast<std::size_t>(y) * sourcePitch + off];
                const std::uint32_t color = palette[index];
                auto * pixel = dst + static_cast<std::size_t>(y) * dstPitch + x * 4u;
                pixel[0] = static_cast<std::uint8_t>(color);
                pixel[1] = static_cast<std::uint8_t>(color >> 8);
                pixel[2] = static_cast<std::uint8_t>(color >> 16);
                pixel[3] = alphaIndexed
                    ? src[static_cast<std::size_t>(y) * sourcePitch + off + 1]
                    : static_cast<std::uint8_t>(color >> 24);
            }
        return true;
    }
    const char * LastError() { return lastError.c_str(); }
}
