// Windowed WARP swap-chain test against the reusable W3D DX12 device.
// This must present real frames, synchronize/reuse back buffers, and survive
// resizing. A copy before Present verifies the clear on an actual GPU buffer.

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
#include <cstdio>
#include <cstring>
#include <iostream>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr wchar_t WindowClassName[] = L"W3DDX12PresentationSmokeWindow";
    constexpr UINT CopyAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

    struct TestWindow
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND window = nullptr;
        bool registered = false;

        bool Create()
        {
            WNDCLASSW cls = {};
            cls.hInstance = instance;
            cls.lpfnWndProc = DefWindowProcW;
            cls.lpszClassName = WindowClassName;
            registered = RegisterClassW(&cls) != 0;
            if (!registered)
                return false;
            window = CreateWindowExW(0, WindowClassName, L"W3D DX12 WARP Presentation",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320, 240,
                nullptr, nullptr, instance, nullptr);
            if (!window)
                return false;
            ShowWindow(window, SW_SHOWNOACTIVATE);
            UpdateWindow(window);
            return true;
        }

        void Pump()
        {
            MSG message = {};
            while (PeekMessageW(&message, window, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        ~TestWindow()
        {
            if (window)
                DestroyWindow(window);
            if (registered)
                UnregisterClassW(WindowClassName, instance);
        }
    };

    bool Check(HRESULT hr, const char * label)
    {
        if (SUCCEEDED(hr))
            return true;
        std::cerr << label << ": 0x" << std::hex
                  << static_cast<unsigned>(hr) << std::dec << "\n";
        return false;
    }

    D3D12_RESOURCE_BARRIER Transition(ID3D12Resource * resource,
                                       D3D12_RESOURCE_STATES before,
                                       D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        return barrier;
    }

    bool PresentAndVerify(DX12PresentationDevice & device,
                          const std::array<float, 4> & color)
    {
        const UINT width = device.Width();
        const UINT height = device.Height();
        const UINT pitch = (width * 4 + CopyAlignment - 1) & ~(CopyAlignment - 1);

        D3D12_HEAP_PROPERTIES readbackHeap = {};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
        readbackHeap.CreationNodeMask = 1;
        readbackHeap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = UINT64(pitch) * height;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> readback;
        if (!Check(device.NativeDevice()->CreateCommittedResource(
                       &readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)),
                   "Create readback buffer"))
            return false;

        if (!device.BeginFrame(color.data()))
        {
            std::cerr << device.LastError() << "\n";
            return false;
        }
        auto * list = device.ActiveCommandList();
        auto * target = device.CurrentBackBuffer();
        if (!list || !target)
            return false;

        const auto toCopy = Transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &toCopy);

        D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = width;
        dst.PlacedFootprint.Footprint.Height = height;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = pitch;
        src.pResource = target;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        const auto toRender = Transition(target, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &toRender);
        if (!device.EndFrame(0) || !device.WaitForIdle())
        {
            std::cerr << device.LastError() << "\n";
            return false;
        }

        const D3D12_RANGE range = {0, SIZE_T(pitch) * height};
        void * mapped = nullptr;
        if (!Check(readback->Map(0, &range, &mapped), "Map frame readback"))
            return false;
        const auto * center = static_cast<const unsigned char *>(mapped) +
                             (height / 2) * pitch + (width / 2) * 4;
        std::array<unsigned char, 4> actual = {center[0], center[1], center[2], center[3]};
        readback->Unmap(0, nullptr);

        for (unsigned i = 0; i < 4; ++i)
        {
            const int expected = static_cast<int>(std::lround(color[i] * 255.0f));
            if (std::abs(int(actual[i]) - expected) > 2)
            {
                std::cerr << "Swapchain pixel " << i << ": expected " << expected
                          << ", got " << unsigned(actual[i]) << "\n";
                return false;
            }
        }
        return true;
    }
}

int wmain(int argc, wchar_t ** argv)
{
    const bool warp = argc == 2 && std::wcscmp(argv[1], L"--warp") == 0;
    const bool hardware = argc == 2 && std::wcscmp(argv[1], L"--hardware") == 0;
    if (!warp && !hardware)
    {
        std::cerr << "Usage: W3DDX12PresentationSmoke --warp|--hardware\n";
        return 2;
    }

    TestWindow window;
    if (!window.Create())
    {
        std::cerr << "Unable to create Win32 presentation window\n";
        return 1;
    }

    DX12PresentationDevice device;
    if (!device.Initialize(window.window, 96, 96, warp))
    {
        std::cerr << "DX12 initialization: " << device.LastError() << "\n";
        return 1;
    }

    // Multiple presents exercise both command allocators and frame fences.
    const std::array<std::array<float, 4>, 3> colors = {{
        {{0.25f, 0.50f, 0.75f, 1.0f}},
        {{0.75f, 0.25f, 0.50f, 1.0f}},
        {{0.10f, 0.80f, 0.30f, 1.0f}}
    }};
    for (const auto & color : colors)
    {
        window.Pump();
        if (!PresentAndVerify(device, color))
            return 1;
    }
    // A minimized window produces zero-sized resize notifications; preserve
    // buffers until a positive client-area size becomes available.
    if (!device.Resize(0, 0) || !device.IsReady())
        return 1;

    if (!device.Resize(128, 72) || device.Width() != 128 || device.Height() != 72)
    {
        std::cerr << "DX12 resize: " << device.LastError() << "\n";
        return 1;
    }
    for (const auto & color : colors)
    {
        window.Pump();
        if (!PresentAndVerify(device, color))
            return 1;
    }

    device.Shutdown();
    if (device.IsReady())
        return 1;
    device.Shutdown(); // Shutdown is intentionally idempotent.
    std::cout << "DX12 WARP swapchain, resize, fences, pixel readback and Present: PASS\n";
    return 0;
}
