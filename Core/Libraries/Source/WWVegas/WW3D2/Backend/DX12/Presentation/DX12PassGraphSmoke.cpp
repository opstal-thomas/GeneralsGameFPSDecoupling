/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/

// DX12-B05: real WARP GPU validation of the future W3D pass sequence,
// offscreen color -> shader-read -> copy state transitions, and fence-safe
// back-buffer return to PRESENT. Does not pretend to draw gameplay assets.
#include "../DX12PresentationDevice.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <cstring>

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
            cls.lpszClassName = L"DX12B05WARPGraph";
            registered = RegisterClassW(&cls) != 0;
            if (!registered)
                return false;
            hwnd = CreateWindowExW(0, cls.lpszClassName, L"DX12 B05 WARP pass graph",
                                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                    320, 240, nullptr, nullptr, instance, nullptr);
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
                UnregisterClassW(L"DX12B05WARPGraph", instance);
        }
    };

    bool Check(bool condition, const char * message)
    {
        if (!condition)
            std::cerr << "DX12-B05 WARP: " << message << "\n";
        return condition;
    }

    bool CreateOffscreenColor(ID3D12Device * device,
                               ComPtr<ID3D12Resource> & target,
                               ComPtr<ID3D12DescriptorHeap> & viewHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = 1;
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&viewHeap))))
            return false;

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 64;
        desc.Height = 64;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (FAILED(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&target))))
            return false;
        device->CreateRenderTargetView(target.Get(), nullptr,
            viewHeap->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    bool CreateReadback(ID3D12Device * device, ComPtr<ID3D12Resource> & readback)
    {
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = 64 * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.SampleDesc.Count = 1;
        return SUCCEEDED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    }

    void CopyOffscreen(ID3D12GraphicsCommandList * list,
                       ID3D12Resource * offscreen,
                       ID3D12Resource * readback)
    {
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = offscreen;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = 64;
        dst.PlacedFootprint.Footprint.Height = 64;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    bool VerifyReadback(ID3D12Resource * readback,
                         const std::array<float, 4> & expected)
    {
        const D3D12_RANGE readRange = {0, 64 * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT};
        void * data = nullptr;
        if (FAILED(readback->Map(0, &readRange, &data)))
            return false;
        const unsigned char * px = static_cast<const unsigned char *>(data) +
                                   32 * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT + 32 * 4;
        bool matched = true;
        for (unsigned i = 0; i < 4; ++i)
            if (std::abs(static_cast<int>(px[i]) -
                         static_cast<int>(std::lround(expected[i] * 255.f))) > 2)
                matched = false;
        readback->Unmap(0, nullptr);
        return matched;
    }

    DX12W3DPassBindings Pass(ID3D12Resource * color,
                              ID3D12Resource * depth = nullptr,
                              ID3D12Resource * const * reads = nullptr,
                              std::size_t readCount = 0)
    {
        DX12W3DPassBindings bindings = {};
        bindings.colorTarget = color;
        bindings.depthTarget = depth;
        bindings.shaderInputs = reads;
        bindings.shaderInputCount = readCount;
        return bindings;
    }
}

int wmain()
{
    TestWindow window;
    if (!Check(window.Create(), "Cannot create developer HWND"))
        return 1;

    DX12PresentationDevice device;
    if (!Check(device.Initialize(window.hwnd, 96, 96, true), "WARP init"))
    {
        std::cerr << device.LastError() << "\n";
        return 1;
    }

    ComPtr<ID3D12Resource> offscreen, readback, unregistered;
    ComPtr<ID3D12DescriptorHeap> offscreenRtv;
    if (!Check(CreateOffscreenColor(device.NativeDevice(), offscreen, offscreenRtv),
               "Create offscreen render target") ||
        !Check(CreateReadback(device.NativeDevice(), readback),
               "Create WARP GPU readback buffer"))
        return 1;

    // A valid but unregistered resource must not be transitioned from a
    // guessed starting state. This must fail BEFORE any GPU barrier is emitted.
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = uploadHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = 256;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.SampleDesc.Count = 1;
    if (!Check(SUCCEEDED(device.NativeDevice()->CreateCommittedResource(
                    &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&unregistered))), "Create unregistered upload buffer"))
        return 1;

    // A default-heap UAV buffer exercises compute-only pass outputs and
    // write-after-write memory ordering without executing a W3D shader.
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = defaultHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC uavDesc = bufferDesc;
    uavDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> uavBuffer;
    if (!Check(SUCCEEDED(device.NativeDevice()->CreateCommittedResource(
                   &defaultHeap, D3D12_HEAP_FLAG_NONE, &uavDesc,
                   D3D12_RESOURCE_STATE_COMMON, nullptr,
                   IID_PPV_ARGS(&uavBuffer))), "Create UAV output buffer"))
        return 1;
    ID3D12Resource * uavOutputs[] = {uavBuffer.Get()};

    const std::array<float, 4> clear = {{0.15f, 0.42f, 0.68f, 1.f}};
    if (!Check(!device.BeginRenderPass(DX12W3DPass::OpaqueWorld, Pass(offscreen.Get())),
               "Reject pass outside frame") ||
        !Check(device.BeginFrame(clear.data()), "Open B05 frame") ||
        !Check(device.FrameBarrierCount() == 1,
               "Exactly one PRESENT -> RENDER_TARGET transition") ||
        !Check(device.RegisterFrameResource(offscreen.Get(), D3D12_RESOURCE_STATE_COMMON),
               "Register actual offscreen COMMON state") ||
        !Check(device.RegisterFrameResource(uavBuffer.Get(), D3D12_RESOURCE_STATE_COMMON),
               "Register actual UAV COMMON state") ||
        !Check(!device.TransitionFrameResource(unregistered.Get(),
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
               "Reject unregistered resource") ||
        !Check(device.RegisterFrameResource(unregistered.Get(),
                    D3D12_RESOURCE_STATE_GENERIC_READ),
               "Register upload buffer in its actual generic-read state") ||
        !Check(device.BeginRenderPass(DX12W3DPass::OpaqueWorld,
                                     Pass(offscreen.Get(), device.CurrentDepthTarget())),
               "Opaque world stage and offscreen target"))
        return 1;

    D3D12_RESOURCE_STATES tracked = D3D12_RESOURCE_STATE_COMMON;
    if (!Check(device.GetFrameResourceState(offscreen.Get(), tracked) &&
               tracked == D3D12_RESOURCE_STATE_RENDER_TARGET,
               "Actual offscreen RT barrier recorded"))
        return 1;
    device.ActiveCommandList()->ClearRenderTargetView(
        offscreenRtv->GetCPUDescriptorHandleForHeapStart(), clear.data(), 0, nullptr);

    ID3D12Resource * inputs[] = {offscreen.Get()};
    const unsigned beforeInvalid = device.FrameBarrierCount();
    if (!Check(!device.BeginRenderPass(DX12W3DPass::Terrain,
                   Pass(unregistered.Get())), "Reject buffer without ALLOW_RENDER_TARGET") ||
        !Check(!device.BeginRenderPass(DX12W3DPass::Terrain,
                   Pass(offscreen.Get(), nullptr, inputs, 1)),
               "Reject reading and writing the same texture within one pass") ||
        !Check(device.FrameBarrierCount() == beforeInvalid,
               "Invalid pass cannot partially submit barriers") ||
        !Check(device.CurrentRenderPassOrdinal() ==
                   static_cast<int>(DX12W3DPass::OpaqueWorld),
               "Rejected pass must not advance stage") ||
        !Check(device.BeginRenderPass(DX12W3DPass::Terrain,
                   Pass(device.CurrentBackBuffer(), device.CurrentDepthTarget(),
                        inputs, 1)), "Terrain stage can sample opaque offscreen"))
        return 1;

    DX12W3DPassBindings effects = Pass(device.CurrentBackBuffer(), nullptr, inputs, 1);
    effects.uavOutputs = uavOutputs;
    effects.uavOutputCount = 1;
    DX12W3DPassBindings transparency = Pass(
        device.CurrentBackBuffer(), device.CurrentDepthTarget(), inputs, 1);
    transparency.uavOutputs = uavOutputs;
    transparency.uavOutputCount = 1;

    if (!Check(device.GetFrameResourceState(offscreen.Get(), tracked) &&
               tracked == (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
               "Offscreen RT to SRV barrier") ||
        !Check(!device.BeginRenderPass(DX12W3DPass::OpaqueWorld,
                   Pass(device.CurrentBackBuffer())),
               "Reject out-of-order opaque pass") ||
        !Check(device.BeginRenderPass(DX12W3DPass::Water,
                   Pass(device.CurrentBackBuffer(), nullptr, inputs, 1)), "Water stage") ||
        !Check(device.BeginRenderPass(DX12W3DPass::Effects, effects),
               "Effects stage with UAV output") ||
        !Check(device.GetFrameResourceState(uavBuffer.Get(), tracked) &&
               tracked == D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               "UAV buffer transitioned to native UNORDERED_ACCESS") ||
        !Check(device.BeginRenderPass(DX12W3DPass::Transparency, transparency),
               "Transparency stage orders consecutive UAV writes") ||
        !Check(device.BeginRenderPass(DX12W3DPass::Shroud,
                   Pass(device.CurrentBackBuffer(), nullptr, inputs, 1)), "Shroud stage") ||
        !Check(device.BeginRenderPass(DX12W3DPass::UI,
                   Pass(device.CurrentBackBuffer(), nullptr, inputs, 1)), "UI stage") ||
        !Check(device.BeginRenderPass(DX12W3DPass::PostProcess,
                   Pass(device.CurrentBackBuffer(), nullptr, inputs, 1)), "PostProcess stage") ||
        !Check(device.CurrentRenderPassOrdinal() == 7, "Canonical final stage") ||
        !Check(!device.BeginRenderPass(DX12W3DPass::UI,
                   Pass(device.CurrentBackBuffer())), "No pass rewind after postprocess") ||
        !Check(!device.BeginRenderPass(DX12W3DPass::Count,
                   Pass(device.CurrentBackBuffer())), "Reject invalid stage"))
        return 1;

    // Read back the offscreen texture after an actual RT -> SRV -> COPY_SOURCE
    // chain. This verifies that the GPU sees the original RTV clear color.
    if (!Check(device.TransitionFrameResource(offscreen.Get(),
               D3D12_RESOURCE_STATE_COPY_SOURCE), "Offscreen SRV -> COPY_SOURCE") ||
        !Check(device.FrameBarrierCount() >= 4, "Actual native transition count"))
        return 1;
    CopyOffscreen(device.ActiveCommandList(), offscreen.Get(), readback.Get());

    // The primary depth target can be copied/read in a later pass. The
    // graph must restore its persistent DEPTH_WRITE invariant at EndFrame,
    // before the subsequent BeginFrame assumes that starting state.
    if (!Check(device.TransitionFrameResource(device.CurrentDepthTarget(),
               D3D12_RESOURCE_STATE_COPY_SOURCE), "Depth write -> copy read"))
        return 1;
    const unsigned barriersBeforeEnd = device.FrameBarrierCount();
    if (!Check(device.EndFrame(0), "Return swap chain to PRESENT + execute") ||
        !Check(device.FrameBarrierCount() == barriersBeforeEnd + 2,
               "Both primary color and depth restore their frame-end state") ||
        !Check(device.WaitForIdle(), "Wait WARP GPU completion") ||
        !Check(VerifyReadback(readback.Get(), clear), "GPU readback of offscreen stage") ||
        !Check(!device.GetFrameResourceState(offscreen.Get(), tracked),
               "Forget external resource registration after frame"))
    {
        std::cerr << device.LastError() << "\n";
        return 1;
    }

    // A second frame may skip stages without violating ordering.
    if (!Check(device.BeginFrame(clear.data()), "Begin next frame") ||
        !Check(device.CurrentRenderPassOrdinal() == -1, "Reset stage on frame reuse") ||
        !Check(!device.TransitionFrameResource(offscreen.Get(),
                                              D3D12_RESOURCE_STATE_COPY_SOURCE),
               "External resource must be re-registered each frame") ||
        !Check(device.BeginRenderPass(DX12W3DPass::UI,
                                     Pass(device.CurrentBackBuffer())), "Skip directly to UI") ||
        !Check(device.RegisterFrameResource(uavBuffer.Get(),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
               "Register retained UAV state for following frame") ||
        !Check(device.BeginRenderPass(DX12W3DPass::PostProcess,
                    [&]() {
                        DX12W3DPassBindings compute = {};
                        compute.uavOutputs = uavOutputs;
                        compute.uavOutputCount = 1;
                        return compute;
                    }()), "Postprocess UAV-only pass without color or depth target") ||
        !Check(device.EndFrame(0, false), "Submit without Present"))
        return 1;

    if (!Check(device.Resize(128, 80), "Resize native swap chain") ||
        !Check(device.BeginFrame(clear.data()), "Frame after swap-chain resize") ||
        !Check(device.FrameBarrierCount() == 1,
               "Fresh PRESENT -> RENDER_TARGET transition after resize") ||
        !Check(device.BeginRenderPass(DX12W3DPass::OpaqueWorld,
                   Pass(device.CurrentBackBuffer(), device.CurrentDepthTarget())),
               "Use recreated color/depth targets") ||
        !Check(device.EndFrame(0), "Present after resize") ||
        !Check(device.WaitForIdle(), "Wait final frame"))
        return 1;

    offscreen.Reset();
    readback.Reset();
    unregistered.Reset();
    uavBuffer.Reset();
    offscreenRtv.Reset();
    device.Shutdown();
    std::cout << "DX12-B05 PASS: 8 canonical W3D stages, tracked D3D12 barriers, "
                 "offscreen GPU readback, UAV memory ordering and frame/resize recovery\n";
    return 0;
}
