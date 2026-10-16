/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
// R07: actual WARP color/depth/stencil target switching, MSAA resolve,
// B05 state tracking, fence-safe views, GPU readback and resize/device reset.
#include "../DX12PresentationDevice.h"
#include "../DX12W3DRenderTargets.h"
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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

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
            cls.lpszClassName = L"W3DDX12R07WARP";
            registered = RegisterClassW(&cls) != 0;
            if (!registered) return false;
            hwnd = CreateWindowExW(0, cls.lpszClassName, L"R07 targets and MSAA",
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                   300, 240, nullptr, nullptr, instance, nullptr);
            return hwnd != nullptr;
        }
        ~Window()
        {
            if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
            if (registered) UnregisterClassW(L"W3DDX12R07WARP", instance);
        }
    };

    bool Check(bool value, const char * message)
    {
        if (!value) std::cerr << "DX12-R07 FAIL " << message << "\n";
        return value;
    }

    struct Readback
    {
        ComPtr<ID3D12Resource> data;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT rows = 0;
        UINT64 rowBytes = 0, bytes = 0;
    };

    bool CopyColor(DX12PresentationDevice & frame, RenderTexture2DHandle texture,
                   Readback & out)
    {
        const auto handle = reinterpret_cast<RenderTextureHandle>(texture);
        auto * resource = DX12W3DTextures::NativeResource(handle);
        if (!resource || !frame.ActiveCommandList()) return false;
        const auto desc = resource->GetDesc();
        frame.NativeDevice()->GetCopyableFootprints(
            &desc, 0, 1, 0, &out.footprint, &out.rows, &out.rowBytes, &out.bytes);
        if (out.bytes == 0) return false;
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC read = {};
        read.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        read.Width = out.bytes;
        read.Height = 1;
        read.DepthOrArraySize = read.MipLevels = 1;
        read.SampleDesc.Count = 1;
        read.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(frame.NativeDevice()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &read,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&out.data))))
            return false;
        if (!DX12W3DTextures::TransitionAttachment(
                frame, handle, D3D12_RESOURCE_STATE_COPY_SOURCE))
            return false;
        D3D12_TEXTURE_COPY_LOCATION source = {}, dest = {};
        source.pResource = resource;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dest.pResource = out.data.Get();
        dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dest.PlacedFootprint = out.footprint;
        frame.ActiveCommandList()->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
        return DX12W3DTextures::TransitionAttachment(
            frame, handle, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    bool Pixel(const Readback & read, const std::array<unsigned, 4> & expected)
    {
        const D3D12_RANGE range = {0, static_cast<SIZE_T>(read.bytes)};
        void * data = nullptr;
        if (FAILED(read.data->Map(0, &range, &data))) return false;
        const unsigned char * color = static_cast<const unsigned char *>(data) +
            read.footprint.Offset +
            static_cast<std::size_t>(read.footprint.Footprint.RowPitch) * 16 + 16 * 4;
        bool matches = true;
        for (unsigned channel = 0; channel < 4; ++channel)
            if (std::abs(static_cast<int>(color[channel]) -
                         static_cast<int>(expected[channel])) > 2)
                matches = false;
        const D3D12_RANGE none = {0, 0};
        read.data->Unmap(0, &none);
        return matches;
    }
}

int wmain()
{
    Window window;
    if (!Check(window.Create(), "Create HWND")) return 1;
    DX12PresentationDevice frame;
    DX12W3DRenderTargets targets;
    if (!Check(frame.Initialize(window.hwnd, 128, 96, true), "Initialize WARP"))
        return 1;
    RenderSurfaceDescription desc;
    desc.width = desc.height = 64;
    desc.pixelFormat = 2; // WW3D A8R8G8B8 / native BGRA8
    auto targetTexture = targets.CreateOffscreenTexture(frame, desc);
    RenderTexture2DDescription depthDesc;
    depthDesc.width = depthDesc.height = 64;
    depthDesc.pixelFormat = 4; // WW3D_ZFORMAT_D24S8
    depthDesc.mipLevels = 1;
    auto depthTexture = targets.CreateDepthTexture(frame, depthDesc);
    auto colorSurface = targets.AcquireTextureLevel(frame, targetTexture, 0);
    auto depthSurface = targets.AcquireTextureLevel(frame, depthTexture, 0);
    if (!Check(targetTexture && depthTexture && colorSurface && depthSurface,
               "Create native BGRA8 RTV and D24S8 DSV surfaces"))
        return 1;
    RenderSurfaceDescription info;
    if (!Check(targets.Describe(colorSurface, info) &&
               info.width == 64 && info.height == 64 && info.pixelFormat == 2,
               "Describe retained native offscreen RTV") ||
        !Check(!targets.AcquireTextureLevel(frame, targetTexture, 1),
               "Reject invalid render-target subresource"))
        return 1;

    // No implicit MSAA downgrade: a single-sample depth target may not be
    // attached to a multisampled color target, even when dimensions match.
    auto msaa = targets.CreateMSAAColor(frame, 64, 64, 2, 4);
    if (!Check(msaa != nullptr, "Create supported 4x MSAA color with resolve texture"))
        return 1;
    auto resolved = targets.ResolvedTexture(msaa);
    DX12W3DTextures::Retain(reinterpret_cast<RenderTextureHandle>(resolved));
    if (!Check(resolved != nullptr &&
               DX12W3DTextures::NativeResource(
                   reinterpret_cast<RenderTextureHandle>(resolved)) != nullptr,
               "MSAA output has a real single-sample shader-readable resolve target"))
        return 1;
    RenderSurfaceDescription invalid = desc;
    invalid.multisampled = true;
    if (!Check(!targets.CreateOffscreenTexture(frame, invalid),
               "Legacy W3D RTT refuses mismatched multisample policy"))
        return 1;

    // BeginFrame enters B05's real command list and registers primary state.
    const float primary[] = {0.f, 0.f, 0.f, 1.f};
    const float color[] = {0.20f, 0.40f, 0.60f, 1.f};
    const float msaaColor[] = {0.80f, 0.10f, 0.20f, 1.f};
    if (!Check(frame.BeginFrame(primary), "Open GPU command list") ||
        !Check(targets.BeginFrame(frame), "Begin R07 frame"))
        return 1;
    auto savedPrimary = targets.AcquirePrimaryColor(frame);
    auto savedDepth = targets.AcquirePrimaryDepth(frame);
    if (!Check(savedPrimary && savedDepth, "Acquire original native game RTV/DSV"))
        return 1;
    const unsigned barriersBeforeInvalid = frame.FrameBarrierCount();
    RenderTexture2DDescription otherDepthDesc = depthDesc;
    otherDepthDesc.width = 32;
    auto otherDepth = targets.CreateDepthTexture(frame, otherDepthDesc);
    auto mismatched = targets.AcquireTextureLevel(frame, otherDepth, 0);
    if (!Check(otherDepth && mismatched && !targets.Bind(frame, colorSurface, mismatched),
               "Reject offscreen RTV/DSV dimensions mismatch") ||
        !Check(frame.FrameBarrierCount() == barriersBeforeInvalid,
               "Rejected target pairing records no partial transitions") ||
        !Check(!targets.Bind(frame, msaa, depthSurface),
               "Reject MSAA 4x color with single-sample DSV"))
        return 1;
    targets.Release(mismatched);
    DX12W3DTextures::Release(reinterpret_cast<RenderTextureHandle>(otherDepth));

    if (!Check(targets.Bind(frame, colorSurface, depthSurface),
               "Switch GPU RTV/DSV from HWND to offscreen") ||
        !Check(targets.BoundColorResource(frame) ==
               DX12W3DTextures::NativeResource(
                   reinterpret_cast<RenderTextureHandle>(targetTexture)),
               "R07 OM binds the shared R04 native texture") ||
        !Check(frame.FrameBarrierCount() > barriersBeforeInvalid,
               "B05 recorded native transitions for offscreen outputs") ||
        !Check(targets.Clear(frame, true, true, color, 0.375f, 0x69),
               "Clear BGRA8 color and D24S8 depth/stencil on real WARP GPU"))
        return 1;
    auto active = targets.AcquireCurrentColor(frame);
    if (!Check(active && targets.TextureForSurface(active) ==
               reinterpret_cast<RenderTextureHandle>(targetTexture),
               "Acquire currently selected offscreen color"))
        return 1;
    targets.Release(active);
    // Drop the original W3D CPU owner before the command buffer completes.
    DX12W3DTextures::Release(reinterpret_cast<RenderTextureHandle>(targetTexture));
    targetTexture = nullptr;
    if (!Check(targets.Bind(frame, nullptr, nullptr),
               "Restore HWND RTV and DSV, transition offscreen color to SRV"))
        return 1;
    Readback offscreenPixels, multisamplePixels;
    auto offscreenTexture = targets.TextureForSurface(colorSurface);
    if (!Check(CopyColor(frame,
               reinterpret_cast<RenderTexture2DHandle>(offscreenTexture),
               offscreenPixels), "GPU readback offscreen RTV pixels"))
        return 1;

    if (!Check(targets.Bind(frame, msaa, nullptr),
               "Bind 4x MSAA RTV without invalid single-sample depth") ||
        !Check(targets.Clear(frame, true, false, msaaColor, 1.f, 0),
               "Clear native 4x multisampled color") ||
        !Check(targets.Bind(frame, savedPrimary, savedDepth),
               "Resolve MSAA and restore saved HWND surfaces"))
        return 1;
    if (!Check(CopyColor(frame, resolved, multisamplePixels),
               "Read back real native ResolveSubresource output"))
        return 1;
    // Acquired game surfaces must not keep swapchain buffers alive across
    // resize. Their serial prevents reuse in a subsequent command recording.
    targets.Release(savedDepth);
    targets.Release(depthSurface);
    DX12W3DTextures::Release(reinterpret_cast<RenderTextureHandle>(depthTexture));
    targets.Release(colorSurface);
    targets.Release(msaa);
    if (!Check(targets.EndFrame(frame), "Close R07 target recording") ||
        !Check(frame.EndFrame(0), "Execute GPU frame and present") ||
        !Check(frame.WaitForIdle(), "Wait WARP fence") ||
        !Check(Pixel(offscreenPixels, {51, 102, 153, 255}),
               "Offscreen RTV clear matches GPU BGRA readback") ||
        !Check(Pixel(multisamplePixels, {204, 26, 51, 255}),
               "MSAA resolve color matches WARP GPU readback") ||
        !Check(frame.InFlightResourceCount() == 0 &&
               frame.InFlightObjectCount() == 0,
               "Offscreen attachments and descriptor views retired by fence"))
        return 1;

    if (!Check(frame.Resize(160, 120), "Resize primary backbuffer/depth") ||
        !Check(frame.BeginFrame(primary), "New primary HWND frame") ||
        !Check(targets.BeginFrame(frame), "New R07 frame after resize") ||
        !Check(!targets.Bind(frame, savedPrimary, nullptr),
               "Reject stale primary surface handle after resize"))
        return 1;
    targets.Release(savedPrimary);
    if (!Check(targets.Bind(frame, nullptr, nullptr), "Restore new primary attachments") ||
        !Check(targets.EndFrame(frame) && frame.EndFrame(0) && frame.WaitForIdle(),
               "Present resized frame"))
        return 1;

    // Surviving textures rehydrate on a newly created DX12 device.
    frame.Shutdown();
    if (!Check(frame.Initialize(window.hwnd, 128, 96, true),
               "Reinitialize native DX12 device") ||
        !Check(frame.BeginFrame(primary) && targets.BeginFrame(frame),
               "Reinitialize B05/R07 after device reset"))
        return 1;
    auto recovered = targets.AcquireTextureLevel(frame, resolved, 0);
    if (!Check(recovered && targets.Bind(frame, recovered, nullptr) &&
               targets.Clear(frame, true, false, color, 1.f, 0) &&
               targets.Bind(frame, nullptr, nullptr) &&
               targets.EndFrame(frame) && frame.EndFrame(0) &&
               frame.WaitForIdle(), "Rehydrate offscreen texture/RTV after device reset"))
        return 1;
    targets.Release(recovered);
    DX12W3DTextures::Release(reinterpret_cast<RenderTextureHandle>(resolved));
    frame.Shutdown();
    std::cout << "R07 PASS: native RTV/DSV, stencil, MSAA resolve, GPU pixels, "
                 "B05 barriers, fence-safe early release, resize and reset\n";
    return 0;
}
