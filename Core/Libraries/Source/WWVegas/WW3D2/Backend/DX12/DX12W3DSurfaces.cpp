/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#include "DX12W3DSurfaces.h"
#include "DX12W3DTextures.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace
{
    thread_local std::string lastError;
    bool Fail(const char * reason) { lastError = reason; return false; }

    unsigned PixelBytes(unsigned format)
    {
        switch (format)
        {
            case 1: return 3;            // WW3D_FORMAT_R8G8B8
            case 2: case 3: case 19: return 4; // ARGB/XRGB/X8L8V8U8
            case 4: case 5: case 6: case 7:
            case 10: case 11: case 15: case 17: case 18: return 2;
            case 8: case 9: case 14: case 16: return 1;
            default: return 0;          // P8/A8P8 or compressed, convert first.
        }
    }

    struct Surface
    {
        std::atomic<unsigned> refs{1};
        std::mutex mutex;
        RenderSurfaceDescription description;
        unsigned bpp = 0, pitch = 0;
        std::vector<std::uint8_t> pixels;
        bool locked = false;
    };

    Surface * Native(RenderSurfaceHandle handle)
    {
        return reinterpret_cast<Surface *>(handle);
    }

    bool Region(const Surface & s, const RenderSurfaceRect & r)
    {
        return r.left >= 0 && r.top >= 0 && r.right > r.left &&
               r.bottom > r.top &&
               static_cast<unsigned>(r.right) <= s.description.width &&
               static_cast<unsigned>(r.bottom) <= s.description.height;
    }
}

namespace DX12W3DSurfaces
{
    RenderSurfaceHandle Create(const RenderSurfaceDescription & desc)
    {
        lastError.clear();
        const unsigned bytes = PixelBytes(desc.pixelFormat);
        if (!desc.Is_Valid() || desc.multisampled || !bytes ||
            desc.width > 16384 || desc.height > 16384 ||
            static_cast<std::uint64_t>(desc.width) * desc.height * bytes > 256ull * 1024 * 1024)
        {
            Fail("Unsupported W3D CPU surface dimensions/format");
            return nullptr;
        }
        auto * image = new (std::nothrow) Surface;
        if (!image) { Fail("Failed to allocate native image surface"); return nullptr; }
        image->description = desc;
        image->bpp = bytes;
        image->pitch = desc.width * bytes;
        image->pixels.resize(static_cast<std::size_t>(image->pitch) * desc.height, 0);
        return reinterpret_cast<RenderSurfaceHandle>(image);
    }

    RenderSurfaceHandle LoadTGAFile(const char * filename)
    {
        lastError.clear();
        if (!filename || !*filename)
        {
            Fail("TGA filename is empty");
            return nullptr;
        }
        std::ifstream stream(filename, std::ios::binary);
        if (!stream)
        {
            Fail("W3D TGA surface file could not be opened");
            return nullptr;
        }
        stream.seekg(0, std::ios::end);
        const auto length = stream.tellg();
        if (length < 18 || length > static_cast<std::streamoff>(64ull * 1024 * 1024))
        {
            Fail("Invalid TGA file length");
            return nullptr;
        }
        stream.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> file(static_cast<std::size_t>(length));
        if (!stream.read(reinterpret_cast<char *>(file.data()), length))
        {
            Fail("Reading TGA source failed");
            return nullptr;
        }

        const auto u16 = [&](unsigned offset) -> unsigned
        {
            return static_cast<unsigned>(file[offset] | (file[offset + 1] << 8));
        };
        const unsigned imageType = file[2], paletteStart = u16(3), paletteCount = u16(5);
        const unsigned paletteBits = file[7], width = u16(12), height = u16(14);
        const unsigned pixelBits = file[16], flags = file[17];
        const bool indexed = imageType == 1 || imageType == 9;
        const bool gray = imageType == 3 || imageType == 11;
        const bool color = imageType == 2 || imageType == 10;
        const bool rle = imageType == 9 || imageType == 10 || imageType == 11;
        const unsigned rawBytes = pixelBits / 8;
        const unsigned paletteBytes = paletteBits / 8;
        const unsigned paletteSize = file[1] ? paletteCount * paletteBytes : 0;
        if (!width || !height || width > 4096 || height > 4096 ||
            static_cast<std::uint64_t>(width) * height > 16ull * 1024 * 1024 ||
            (!indexed && !gray && !color) || (indexed && (pixelBits != 8 ||
              file[1] != 1 || (paletteBits != 24 && paletteBits != 32))) ||
            (gray && pixelBits != 8) ||
            (color && pixelBits != 24 && pixelBits != 32) ||
            static_cast<std::size_t>(18 + file[0]) + paletteSize > file.size())
        {
            Fail("Unsupported or malformed W3D TGA palette/format");
            return nullptr;
        }

        std::size_t position = static_cast<std::size_t>(18 + file[0]);
        const std::size_t paletteOffset = position;
        position += paletteSize;
        RenderSurfaceDescription desc;
        desc.width = width; desc.height = height; desc.pixelFormat = 2;
        RenderSurfaceHandle out = Create(desc);
        if (!out) return nullptr;
        Surface & surface = *Native(out);
        const std::size_t totalPixels = static_cast<std::size_t>(width) * height;
        std::size_t written = 0;
        bool valid = true;
        while (written < totalPixels && valid)
        {
            unsigned count = 1;
            bool repeat = false;
            if (rle)
            {
                if (position >= file.size()) { valid = false; break; }
                const std::uint8_t code = file[position++];
                count = (code & 0x7Fu) + 1;
                repeat = (code & 0x80u) != 0;
            }
            if (count > totalPixels - written) { valid = false; break; }
            std::uint8_t repeated[4] = {};
            if (repeat)
            {
                if (position + rawBytes > file.size()) { valid = false; break; }
                std::memcpy(repeated, file.data() + position, rawBytes);
                position += rawBytes;
            }
            for (unsigned i = 0; i < count && valid; ++i)
            {
                std::uint8_t in[4] = {};
                if (repeat)
                    std::memcpy(in, repeated, rawBytes);
                else
                {
                    if (position + rawBytes > file.size()) { valid = false; break; }
                    std::memcpy(in, file.data() + position, rawBytes);
                    position += rawBytes;
                }
                std::uint8_t bgra[4] = {0, 0, 0, 255};
                if (indexed)
                {
                    if (in[0] < paletteStart || in[0] >= paletteStart + paletteCount)
                    {
                        valid = false; break;
                    }
                    const std::size_t off = paletteOffset +
                        (static_cast<unsigned>(in[0]) - paletteStart) * paletteBytes;
                    std::memcpy(bgra, file.data() + off, paletteBytes);
                }
                else if (gray)
                    bgra[0] = bgra[1] = bgra[2] = in[0];
                else
                    std::memcpy(bgra, in, rawBytes);

                const unsigned row = static_cast<unsigned>(written / width);
                const unsigned column = static_cast<unsigned>(written % width);
                const unsigned targetY = flags & 0x20u ? row : height - 1 - row;
                const unsigned targetX = flags & 0x10u ? width - 1 - column : column;
                std::memcpy(surface.pixels.data() +
                            (static_cast<std::size_t>(targetY) * width + targetX) * 4u,
                            bgra, 4u);
                ++written;
            }
        }
        if (!valid || written != totalPixels)
        {
            Release(out);
            Fail("Truncated RLE or invalid W3D TGA image/palette data");
            return nullptr;
        }
        return out;
    }

    void Retain(RenderSurfaceHandle handle)
    {
        if (handle) Native(handle)->refs.fetch_add(1, std::memory_order_relaxed);
    }
    void Release(RenderSurfaceHandle handle)
    {
        if (handle && Native(handle)->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete Native(handle);
    }
    bool Describe(RenderSurfaceHandle handle, RenderSurfaceDescription & output)
    {
        output = RenderSurfaceDescription();
        if (!handle) return Fail("Cannot describe null CPU image surface");
        output = Native(handle)->description;
        return true;
    }
    unsigned RowPitch(RenderSurfaceHandle handle) { return handle ? Native(handle)->pitch : 0; }

    bool Lock(RenderSurfaceHandle handle, RenderSurfaceMap & map,
              const RenderSurfaceRect * region, bool)
    {
        lastError.clear();
        map = RenderSurfaceMap();
        if (!handle) return Fail("Null CPU surface lock");
        Surface & s = *Native(handle);
        std::lock_guard<std::mutex> guard(s.mutex);
        const RenderSurfaceRect full = {0, 0,
            static_cast<int>(s.description.width), static_cast<int>(s.description.height)};
        const RenderSurfaceRect & rect = region ? *region : full;
        if (s.locked || !Region(s, rect))
            return Fail("Nested CPU surface lock or invalid rectangle");
        s.locked = true;
        map.data = s.pixels.data() + static_cast<std::size_t>(rect.top) * s.pitch +
                   static_cast<std::size_t>(rect.left) * s.bpp;
        map.rowPitch = static_cast<int>(s.pitch);
        return true;
    }

    bool Unlock(RenderSurfaceHandle handle)
    {
        lastError.clear();
        if (!handle) return Fail("Null CPU surface unlock");
        Surface & s = *Native(handle);
        std::lock_guard<std::mutex> guard(s.mutex);
        if (!s.locked) return Fail("Surface unlock without corresponding lock");
        s.locked = false;
        return true;
    }

    bool Copy(RenderSurfaceHandle from, const RenderSurfaceRect * region,
              RenderSurfaceHandle to, const RenderSurfacePoint * position)
    {
        lastError.clear();
        if (!from || !to) return Fail("CPU surface copy requires source and target");
        Surface & a = *Native(from), & b = *Native(to);
        const auto work = [&]() -> bool
        {
            const RenderSurfaceRect full = {0, 0,
                static_cast<int>(a.description.width), static_cast<int>(a.description.height)};
            const RenderSurfaceRect & rect = region ? *region : full;
            const RenderSurfacePoint origin = position ? *position : RenderSurfacePoint{0, 0};
            if (a.locked || b.locked || a.bpp != b.bpp ||
                a.description.pixelFormat != b.description.pixelFormat ||
                !Region(a, rect) || origin.x < 0 || origin.y < 0 ||
                static_cast<unsigned long long>(origin.x) + rect.right - rect.left > b.description.width ||
                static_cast<unsigned long long>(origin.y) + rect.bottom - rect.top > b.description.height)
                return Fail("CPU surfaces have incompatible formats or invalid copy bounds");
            const std::size_t rowBytes = static_cast<std::size_t>(rect.right - rect.left) * a.bpp;
            std::vector<std::uint8_t> temporary(rowBytes * (rect.bottom - rect.top));
            for (int y = 0; y < rect.bottom - rect.top; ++y)
                std::memcpy(temporary.data() + y * rowBytes,
                            a.pixels.data() + static_cast<std::size_t>(rect.top + y) * a.pitch +
                            static_cast<std::size_t>(rect.left) * a.bpp, rowBytes);
            for (int y = 0; y < rect.bottom - rect.top; ++y)
                std::memcpy(b.pixels.data() + static_cast<std::size_t>(origin.y + y) * b.pitch +
                            static_cast<std::size_t>(origin.x) * b.bpp,
                            temporary.data() + y * rowBytes, rowBytes);
            return true;
        };
        if (from == to) { std::lock_guard<std::mutex> guard(a.mutex); return work(); }
        std::scoped_lock guard(a.mutex, b.mutex);
        return work();
    }

    bool Blit(RenderSurfaceHandle from, const RenderSurfaceRect & source,
              RenderSurfaceHandle to, const RenderSurfaceRect & target,
              RenderSurfaceBlitFilter filter)
    {
        lastError.clear();
        if (!from || !to) return Fail("Blit requires image source and destination");
        Surface & a = *Native(from), & b = *Native(to);
        const auto work = [&]() -> bool
        {
            if (a.locked || b.locked || !Region(a, source) || !Region(b, target) ||
                a.description.pixelFormat != b.description.pixelFormat ||
                (filter != RENDER_SURFACE_BLIT_NONE &&
                 filter != RENDER_SURFACE_BLIT_TRIANGLE))
                return Fail("Invalid W3D image blit/format");
            const int sw = source.right - source.left, sh = source.bottom - source.top;
            const int dw = target.right - target.left, dh = target.bottom - target.top;
            const unsigned bytes = a.bpp;
            std::vector<std::uint8_t> input(static_cast<std::size_t>(sw) * sh * bytes);
            for (int y = 0; y < sh; ++y)
                std::memcpy(input.data() + static_cast<std::size_t>(y) * sw * bytes,
                            a.pixels.data() + static_cast<std::size_t>(source.top + y) * a.pitch +
                            static_cast<std::size_t>(source.left) * bytes, sw * bytes);
            for (int y = 0; y < dh; ++y)
                for (int x = 0; x < dw; ++x)
                {
                    auto * dest = b.pixels.data() + static_cast<std::size_t>(target.top + y) * b.pitch +
                                   static_cast<std::size_t>(target.left + x) * bytes;
                    // R08 TRIANGLE is bilinear in the original source
                    // pixel-center convention. Never interpolate packed
                    // 16-bit/depth formats as if their individual bytes
                    // were independent colors.
                    if (filter == RENDER_SURFACE_BLIT_TRIANGLE && bytes == 4)
                    {
                        const float fx = (static_cast<float>(x) + 0.5f) * sw / dw - 0.5f;
                        const float fy = (static_cast<float>(y) + 0.5f) * sh / dh - 0.5f;
                        const float px = (std::max)(0.f, (std::min)(fx, float(sw - 1)));
                        const float py = (std::max)(0.f, (std::min)(fy, float(sh - 1)));
                        const int x0 = static_cast<int>(std::floor(px));
                        const int y0 = static_cast<int>(std::floor(py));
                        const int x1 = (std::min)(x0 + 1, sw - 1);
                        const int y1 = (std::min)(y0 + 1, sh - 1);
                        const float wx = px - x0, wy = py - y0;
                        for (unsigned channel = 0; channel < 4; ++channel)
                        {
                            const auto sample = [&](int sx, int sy) -> float
                            {
                                return input[(static_cast<std::size_t>(sy) * sw + sx) * 4 + channel];
                            };
                            const float top = sample(x0, y0) * (1.f - wx) + sample(x1, y0) * wx;
                            const float bottom = sample(x0, y1) * (1.f - wx) + sample(x1, y1) * wx;
                            dest[channel] = static_cast<std::uint8_t>(
                                (std::min)(255.f, top * (1.f - wy) + bottom * wy + 0.5f));
                        }
                    }
                    else
                    {
                        const unsigned sx = (std::min)(static_cast<unsigned>(sw - 1),
                            static_cast<unsigned>((static_cast<std::uint64_t>(x) * sw) / dw));
                        const unsigned sy = (std::min)(static_cast<unsigned>(sh - 1),
                            static_cast<unsigned>((static_cast<std::uint64_t>(y) * sh) / dh));
                        std::memcpy(dest, input.data() + (sy * sw + sx) * bytes, bytes);
                    }
                }
            return true;
        };
        if (from == to) { std::lock_guard<std::mutex> guard(a.mutex); return work(); }
        std::scoped_lock guard(a.mutex, b.mutex);
        return work();
    }

    RenderTexture2DHandle ToTexture(ID3D12Device * device, RenderSurfaceHandle handle,
                                    unsigned mipCount)
    {
        lastError.clear();
        if (!device || !handle) { Fail("Texture source surface/device missing"); return nullptr; }
        Surface & s = *Native(handle);
        std::vector<std::uint8_t> rgba;
        unsigned width = 0, height = 0;
        {
            std::lock_guard<std::mutex> guard(s.mutex);
            if (s.locked) { Fail("Cannot convert a mapped surface"); return nullptr; }
            width = s.description.width; height = s.description.height;
            rgba.resize(static_cast<std::size_t>(width) * height * 4u);
            for (unsigned y = 0; y < height; ++y)
                if (!DX12W3DTextures::ConvertRowToBGRA8(
                        s.description.pixelFormat, s.pixels.data() +
                        static_cast<std::size_t>(y) * s.pitch, width,
                        rgba.data() + static_cast<std::size_t>(y) * width * 4u))
                    return nullptr;
        }
        // Normalize packed/paletted format to four-channel BGRA8 before
        // box downsampling; mips remain independent of D3DX texture filters.
        RenderTexture2DDescription desc;
        desc.width = width; desc.height = height;
        desc.pixelFormat = 2; // WW3D_FORMAT_A8R8G8B8
        desc.mipLevels = mipCount;
        RenderTexture2DHandle tex = DX12W3DTextures::Create2D(device, desc);
        if (!tex) return nullptr;
        unsigned w = width, h = height;
        for (unsigned mip = 0; mip < DX12W3DTextures::MipCount(
                                      reinterpret_cast<RenderTextureHandle>(tex)); ++mip)
        {
            RenderSurfaceMap mapped;
            if (!DX12W3DTextures::Map2D(tex, mip, mapped))
            {
                DX12W3DTextures::Release(reinterpret_cast<RenderTextureHandle>(tex));
                return nullptr;
            }
            for (unsigned y = 0; y < h; ++y)
                std::memcpy(static_cast<std::uint8_t *>(mapped.data) +
                            static_cast<std::size_t>(y) * mapped.rowPitch,
                            rgba.data() + static_cast<std::size_t>(y) * w * 4u, w * 4u);
            DX12W3DTextures::Unmap2D(tex, mip);
            if (w == 1 && h == 1) continue;
            const unsigned nextW = std::max(1u, w / 2), nextH = std::max(1u, h / 2);
            std::vector<std::uint8_t> reduced(static_cast<std::size_t>(nextW) * nextH * 4u);
            for (unsigned y = 0; y < nextH; ++y)
                for (unsigned x = 0; x < nextW; ++x)
                    for (unsigned c = 0; c < 4; ++c)
                    {
                        unsigned sum = 0, samples = 0;
                        for (unsigned dy = 0; dy < 2; ++dy)
                            for (unsigned dx = 0; dx < 2; ++dx)
                            {
                                const unsigned py = std::min(h - 1, y * 2 + dy);
                                const unsigned px = std::min(w - 1, x * 2 + dx);
                                sum += rgba[(static_cast<std::size_t>(py) * w + px) * 4 + c];
                                ++samples;
                            }
                        reduced[(static_cast<std::size_t>(y) * nextW + x) * 4 + c] =
                            static_cast<std::uint8_t>((sum + samples / 2) / samples);
                    }
            rgba.swap(reduced);
            w = nextW; h = nextH;
        }
        return tex;
    }
    const char * LastError() { return lastError.c_str(); }
}
