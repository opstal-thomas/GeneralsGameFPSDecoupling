/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#include "../DX12PresentationDevice.h"
#include "../DX12W3DTextures.h"

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
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    struct Window
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND hwnd = nullptr;
        bool registered = false;
        bool Create()
        {
            WNDCLASSW cls = {};
            cls.hInstance = instance;
            cls.lpfnWndProc = DefWindowProcW;
            cls.lpszClassName = L"W3DDX12R04WARP";
            registered = RegisterClassW(&cls) != 0;
            if (!registered) return false;
            hwnd = CreateWindowExW(0, cls.lpszClassName, L"R04 native texture smoke",
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                   320, 240, nullptr, nullptr, instance, nullptr);
            return hwnd != nullptr;
        }
        ~Window()
        {
            if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
            if (registered) UnregisterClassW(L"W3DDX12R04WARP", instance);
        }
    };
    bool Check(bool ok, const char * what)
    {
        if (!ok)
            std::cerr << "DX12-R04 FAIL " << what << ": "
                      << DX12W3DTextures::LastError() << "\n";
        return ok;
    }

    struct Readback
    {
        ComPtr<ID3D12Resource> resource;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT rows = 0;
        UINT64 rowBytes = 0;
        UINT64 size = 0;
    };

    bool CopyMipToReadback(DX12PresentationDevice & device,
                           RenderTextureHandle texture, unsigned mip, Readback & result)
    {
        auto * gpu = DX12W3DTextures::NativeResource(texture);
        if (!gpu || !device.ActiveCommandList()) return false;
        const D3D12_RESOURCE_DESC desc = gpu->GetDesc();
        if (mip >= desc.MipLevels) return false;
        device.NativeDevice()->GetCopyableFootprints(
            &desc, mip, 1, 0, &result.footprint, &result.rows,
            &result.rowBytes, &result.size);
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC buffer = {};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = result.size;
        buffer.Height = 1;
        buffer.DepthOrArraySize = buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device.NativeDevice()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result.resource))))
            return false;
        if (!device.TransitionFrameResource(gpu, D3D12_RESOURCE_STATE_COPY_SOURCE))
            return false;
        D3D12_TEXTURE_COPY_LOCATION from = {}, to = {};
        from.pResource = gpu;
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.SubresourceIndex = mip;
        to.pResource = result.resource.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint = result.footprint;
        device.ActiveCommandList()->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        const auto sampled = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return device.TransitionFrameResource(gpu, sampled);
    }
    bool EqualsReadback(const Readback & readback, unsigned offset,
                        const std::uint8_t * expected, std::size_t length)
    {
        const D3D12_RANGE range = {0, static_cast<SIZE_T>(readback.size)};
        void * raw = nullptr;
        if (FAILED(readback.resource->Map(0, &range, &raw))) return false;
        const bool same = std::memcmp(static_cast<std::uint8_t *>(raw) +
                                      readback.footprint.Offset + offset, expected, length) == 0;
        const D3D12_RANGE zero = {0, 0};
        readback.resource->Unmap(0, &zero);
        return same;
    }

    void * H2D(RenderTexture2DHandle tex)
    {
        return reinterpret_cast<void *>(tex);
    }
    RenderTextureHandle T(RenderTexture2DHandle h)
    {
        return reinterpret_cast<RenderTextureHandle>(h);
    }
    RenderTextureHandle T(RenderTextureCubeHandle h)
    {
        return reinterpret_cast<RenderTextureHandle>(h);
    }
    RenderTextureHandle T(RenderTextureVolumeHandle h)
    {
        return reinterpret_cast<RenderTextureHandle>(h);
    }
}

int wmain()
{
    using namespace DX12W3DTextures;
    Window window;
    if (!Check(window.Create(), "Create HWND")) return 1;
    DX12PresentationDevice device;
    if (!Check(device.Initialize(window.hwnd, 100, 90, true), "WARP device"))
        return 1;
    RenderTexture2DDescription desc;
    desc.width = desc.height = 4;
    desc.pixelFormat = 2; // WW3D_FORMAT_A8R8G8B8
    desc.mipLevels = 3;
    auto main = Create2D(device.NativeDevice(), desc);
    auto copied = Create2D(device.NativeDevice(), desc);
    if (!Check(main && copied, "Create native mipmapped ARGB8 textures") ||
        !Check(MipCount(T(main)) == 3, "Mip level count") ||
        !Check(MemoryUsage(T(main)) == 84, "W3D CPU mip allocation accounting"))
        return 1;
    RenderTextureLevelDescription level;
    if (!Check(Describe(T(main), 2, level) &&
               level.width == 1 && level.height == 1 && level.depth == 1,
               "Describe 1x1 mip"))
        return 1;
    std::array<std::uint8_t, 64> pixels = {};
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
        {
            const unsigned i = (y * 4 + x) * 4;
            pixels[i] = static_cast<std::uint8_t>(0x10u + x);
            pixels[i+1] = static_cast<std::uint8_t>(0x20u + y);
            pixels[i+2] = 0x33;
            pixels[i+3] = 0xFF;
        }
    RenderSurfaceMap mapped;
    if (!Check(Map2D(main, 0, mapped) && mapped.rowPitch == 16, "Map ARGB8 base mip") ||
        !Check(!Map2D(main, 0, mapped), "Reject nested lock"))
        return 1;
    // Nested lock intentionally zeroed the second output; map again would
    // be invalid, so use the originally returned writable pointer below.
    // Unmap / remap provides a fresh valid returned address for writing.
    if (!Check(Unmap2D(main, 0), "Unmap first lock") ||
        !Check(Map2D(main, 0, mapped), "Remap base mip"))
        return 1;
    std::memcpy(mapped.data, pixels.data(), pixels.size());
    if (!Check(Unmap2D(main, 0), "Commit ARGB8 mip"))
        return 1;
    for (unsigned mip = 1; mip < 3; ++mip)
    {
        if (!Check(Map2D(main, mip, mapped), "Map reduced mip")) return 1;
        std::memset(mapped.data, mip == 1 ? 0x44 : 0x55,
                    (4u >> mip) * (4u >> mip) * 4u);
        if (!Check(Unmap2D(main, mip), "Commit reduced mip")) return 1;
    }
    if (!Check(Update(T(main), T(copied)), "CPU mip-to-mip texture update"))
        return 1;

    RenderTexture2DDescription bcDesc;
    bcDesc.width = bcDesc.height = 8;
    bcDesc.pixelFormat = 20; // WW3D_FORMAT_DXT1
    bcDesc.mipLevels = 2;
    auto bc = Create2D(device.NativeDevice(), bcDesc);
    if (!Check(bc && NativeFormat(T(bc)) == DXGI_FORMAT_BC1_UNORM,
               "Native BC1 DDS compressed GPU format") ||
        !Check(MemoryUsage(T(bc)) == 40, "BC block rows with two mips"))
        return 1;
    const std::array<std::uint8_t, 8> bcBlock = {0x00, 0xF8, 0xE0, 0x07, 0, 0, 0, 0};
    for (unsigned mip = 0; mip < 2; ++mip)
    {
        if (!Check(Map2D(bc, mip, mapped), "Map DDS BC1 mip")) return 1;
        const unsigned blockRows = mip == 0 ? 2u : 1u;
        const unsigned blocksWide = mip == 0 ? 2u : 1u;
        if (!Check(mapped.rowPitch == static_cast<int>(blocksWide * 8),
                   "DDS mip raw BC pitch")) return 1;
        for (unsigned row = 0; row < blockRows; ++row)
            for (unsigned col = 0; col < blocksWide; ++col)
                std::memcpy(static_cast<std::uint8_t *>(mapped.data) +
                            row * mapped.rowPitch + col * 8, bcBlock.data(), 8);
        if (!Check(Unmap2D(bc, mip), "Commit BC1 compressed mip")) return 1;
    }
    RenderTexture2DDescription bc3Desc = bcDesc;
    bc3Desc.pixelFormat = 24; // WW3D_FORMAT_DXT5
    if (!Check(SupportsFormat(24) && SupportsFormat(21) &&
               SupportsFormat(22) && SupportsFormat(23) && !SupportsFormat(13),
               "BC2/BC3 variants supported, palettized P8 requires conversion"))
        return 1;
    auto bc3 = Create2D(device.NativeDevice(), bc3Desc);
    if (!Check(bc3 && NativeFormat(T(bc3)) == DXGI_FORMAT_BC3_UNORM,
               "BC3 native allocation")) return 1;

    RenderTextureCubeDescription cubeDesc;
    cubeDesc.edgeLength = 4;
    cubeDesc.pixelFormat = 2;
    cubeDesc.mipLevels = 2;
    auto cube = CreateCube(device.NativeDevice(), cubeDesc);
    if (!Check(cube && MipCount(T(cube)) == 2,
               "Cube DDS face and mip allocation")) return 1;
    for (unsigned face = 0; face < 6; ++face)
        for (unsigned mip = 0; mip < 2; ++mip)
        {
            if (!Check(MapCube(cube, face, mip, mapped), "Map cube face/mip")) return 1;
            std::memset(mapped.data, (face + 1) * 15 + mip, mapped.rowPitch * (4 >> mip));
            if (!Check(UnmapCube(cube, face, mip), "Commit cube mip")) return 1;
        }
    RenderTextureVolumeDescription volumeDesc;
    volumeDesc.width = volumeDesc.height = 4;
    volumeDesc.depth = 2;
    volumeDesc.pixelFormat = 2;
    volumeDesc.mipLevels = 2;
    auto volume = CreateVolume(device.NativeDevice(), volumeDesc);
    RenderVolumeMap volumeMap;
    if (!Check(volume && MapVolume(volume, 0, volumeMap) &&
               volumeMap.rowPitch == 16 && volumeMap.slicePitch == 64,
               "Volume DDS 3D slice and row pitch")) return 1;
    std::memset(volumeMap.data, 0x28, 128);
    if (!Check(UnmapVolume(volume, 0), "Commit 3D texture"))
        return 1;
    if (!Check(MapVolume(volume, 1, volumeMap), "Volume reduced mip")) return 1;
    std::memset(volumeMap.data, 0x77, 16);
    if (!Check(UnmapVolume(volume, 1), "Volume reduced mip commit"))
        return 1;

    std::uint32_t palette[256] = {};
    palette[2] = 0x80402010u;
    const std::uint8_t indexPixels[] = {2, 2};
    std::uint8_t decoded[4] = {};
    if (!Check(ConvertPaletteToBGRA8(indexPixels, 1, 1, 2, true,
                                     palette, decoded, 4) &&
               decoded[0] == 0x10 && decoded[1] == 0x20 &&
               decoded[2] == 0x40 && decoded[3] == 2,
               "A8P8 indexed W3D palette conversion")) return 1;

    const float clear[] = {0.10f, 0.20f, 0.30f, 1.f};
    if (!Check(device.BeginFrame(clear), "Begin real WARP frame") ||
        !Check(Prepare(device, T(main)), "Upload 2D mip chain") ||
        !Check(Prepare(device, T(copied)), "Upload updated texture") ||
        !Check(Prepare(device, T(bc)), "Upload real BC1 DDS blocks") ||
        !Check(Prepare(device, T(bc3)), "Upload BC3 texture") ||
        !Check(Prepare(device, T(cube)), "Upload cube faces") ||
        !Check(Prepare(device, T(volume)), "Upload volume slices"))
        return 1;
    if (!Check(NativeResource(T(main)) &&
               NativeResource(T(main))->GetDesc().Format == DXGI_FORMAT_B8G8R8A8_UNORM,
               "Actual D3D12 default-heap BGRA8 GPU texture"))
        return 1;
    Readback rgbaReadback, bcReadback, mipReadback, cubeReadback;
    if (!Check(CopyMipToReadback(device, T(main), 0, rgbaReadback),
               "Copy original texture from GPU") ||
        !Check(CopyMipToReadback(device, T(bc), 1, bcReadback),
               "Readback 4x4 compressed mip") ||
        !Check(CopyMipToReadback(device, T(copied), 2, mipReadback),
               "Readback updated 1x1 mip"))
        return 1;

    // Released W3D textures are no longer heap owners; the command-list fence
    // keeps the GPU allocations and upload staging alive until execution ends.
    Release(T(main));
    Release(T(copied));
    Release(T(bc));
    if (!Check(device.InFlightResourceCount() >= 12, "GPU upload staging pinned") ||
        !Check(device.EndFrame(0), "Submit texture copies") ||
        !Check(device.WaitForIdle(), "Wait WARP texture uploads") ||
        !Check(EqualsReadback(rgbaReadback, 0, pixels.data(), 16),
               "GPU RGBA8 first row matches W3D loader bytes") ||
        !Check(EqualsReadback(bcReadback, 0, bcBlock.data(), 8),
               "GPU BC1 compressed mip matches file block") ||
        !Check(EqualsReadback(mipReadback, 0,
                   reinterpret_cast<const std::uint8_t *>("\x55\x55\x55\x55"), 4),
               "GPU UpdateTexture 1x1 mip roundtrip") ||
        !Check(device.InFlightResourceCount() == 0,
               "Staging and texture generations retired by native fence"))
        return 1;

    // The CPU shadow of a retained cube/volume texture survives device
    // shutdown and must be re-uploaded onto the newly created DX12 adapter.
    device.Shutdown();
    if (!Check(device.Initialize(window.hwnd, 100, 90, true), "Recreate native DX12 device") ||
        !Check(device.BeginFrame(clear), "Begin frame after device reset") ||
        !Check(Prepare(device, T(cube)) && Prepare(device, T(volume)) &&
               Prepare(device, T(bc3)), "Rehydrate textures from CPU mip shadows") ||
        !Check(device.EndFrame(0), "Submit reset recovery upload") ||
        !Check(device.WaitForIdle(), "Wait recovered textures"))
        return 1;
    Release(T(cube));
    Release(T(volume));
    Release(T(bc3));
    device.Shutdown();
    std::cout << "R04 PASS: W3D DDS BC mip uploads, packed and uncompressed pixels, "
                 "cube/volume maps, palette, fence lifetime, GPU readback and reset\n";
    return 0;
}
