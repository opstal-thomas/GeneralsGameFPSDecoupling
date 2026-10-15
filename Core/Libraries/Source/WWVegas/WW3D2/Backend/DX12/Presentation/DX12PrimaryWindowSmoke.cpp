// DX12-B03 regression: bind to an existing game's root HWND, retain its
// ownership, and present with WARP while resizing/minimizing/changing styles.
// This is the primary-window path used by the DX12-selected Renderer, not
// the independent, secondary DX12 gameplay-preview window.
#include "../DX12PrimaryWindow.h"

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
#include <iostream>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr wchar_t ClassName[] = L"W3DDX12PrimaryWindowSmoke";

    struct GameWindow
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND hwnd = nullptr;
        bool registered = false;

        bool Create()
        {
            WNDCLASSW cls = {};
            cls.hInstance = instance;
            cls.lpfnWndProc = DefWindowProcW;
            cls.lpszClassName = ClassName;
            registered = RegisterClassW(&cls) != 0;
            if (!registered)
                return false;

            hwnd = CreateWindowExW(0, ClassName, L"W3D DX12 Primary HWND Smoke",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 420, 310,
                nullptr, nullptr, instance, nullptr);
            if (!hwnd)
                return false;
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            UpdateWindow(hwnd);
            return true;
        }

        void Pump() const
        {
            MSG message = {};
            while (PeekMessageW(&message, hwnd, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        ~GameWindow()
        {
            if (hwnd && IsWindow(hwnd))
                DestroyWindow(hwnd);
            if (registered)
                UnregisterClassW(ClassName, instance);
        }
    };

    bool Check(bool ok, const char * step)
    {
        if (!ok)
            std::cerr << "B03 primary window test FAILED at " << step << "\n";
        return ok;
    }

    // Capture a pixel from the *actual* primary-HWND swap chain. The copy
    // is recorded in the active native scene and executed by EndScene.
    bool RecordCenterReadback(DX12PresentationDevice & device,
                              ComPtr<ID3D12Resource> & readback, unsigned & pitch)
    {
        if (!device.IsRecording() || !device.CurrentBackBuffer() ||
            !device.ActiveCommandList())
            return false;
        const unsigned width = device.Width(), height = device.Height();
        pitch = (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(pitch) * height;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.SampleDesc.Count = 1;
        if (FAILED(device.NativeDevice()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))))
            return false;

        auto * buffer = device.CurrentBackBuffer();
        auto * list = device.ActiveCommandList();
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffer;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = width;
        dst.PlacedFootprint.Footprint.Height = height;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = pitch;
        src.pResource = buffer;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1, &barrier);
        return true;
    }

    bool VerifyCenterReadback(DX12PresentationDevice & device,
                              ComPtr<ID3D12Resource> & readback, unsigned pitch,
                              const std::array<float, 4> & color)
    {
        if (!device.WaitForIdle())
            return false;
        const D3D12_RANGE range = {0, static_cast<SIZE_T>(pitch) * device.Height()};
        void * data = nullptr;
        if (FAILED(readback->Map(0, &range, &data)))
            return false;
        const auto * pixel = static_cast<const unsigned char *>(data) +
                             (device.Height() / 2) * pitch +
                             (device.Width() / 2) * 4;
        bool equal = true;
        for (unsigned i = 0; i < 4; ++i)
        {
            const int expected = static_cast<int>(std::lround(color[i] * 255.f));
            if (std::abs(static_cast<int>(pixel[i]) - expected) > 2)
            {
                std::cerr << "B04 native-frame readback channel " << i
                          << ": expected " << expected << " got "
                          << static_cast<unsigned>(pixel[i]) << "\\n";
                equal = false;
            }
        }
        readback->Unmap(0, nullptr);
        return equal;
    }

    bool SetClientArea(HWND hwnd, unsigned width, unsigned height, DWORD style)
    {
        RECT outer = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        if (!AdjustWindowRectEx(&outer, style, FALSE, 0))
            return false;
        return SetWindowPos(hwnd, nullptr, 0, 0,
            outer.right - outer.left, outer.bottom - outer.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED) != FALSE;
    }

    bool MatchesClient(const DX12PrimaryWindow & window, HWND hwnd)
    {
        RECT area = {};
        return GetClientRect(hwnd, &area) &&
               window.Presentation().Width() ==
                   static_cast<unsigned>(area.right - area.left) &&
               window.Presentation().Height() ==
                   static_cast<unsigned>(area.bottom - area.top);
    }
}

int wmain()
{
    GameWindow game;
    if (!Check(game.Create(), "Create game-owned root HWND"))
        return 1;

    DX12PrimaryWindow primary;
    if (!Check(!primary.Attach(nullptr, true), "Reject null HWND") ||
        !Check(primary.Attach(game.hwnd, true), "Attach WARP to real game HWND"))
    {
        std::cerr << primary.LastError() << "\n";
        return 1;
    }
    if (!Check(primary.Window() == game.hwnd, "Never create another window") ||
        !Check(primary.CurrentStatus() == DX12PrimaryWindow::Status::Ready, "Ready") ||
        !Check(primary.Presentation().NativeDevice() != nullptr, "Native D3D12 device") ||
        !Check(!primary.IsBorderless(), "Start in windowed mode") ||
        !Check(MatchesClient(primary, game.hwnd), "Initial back-buffer client size"))
        return 1;

    // B04 regression: W3D queues color/depth Clear before Begin_Scene. Verify
    // the deferred color reaches the actual GPU back buffer, without the B03
    // PresentDiagnostic helper obscuring a missing clear.
    const std::array<float, 4> deferred = {{0.22f, 0.48f, 0.73f, 1.f}};
    if (!Check(!primary.EndScene(true, 0), "Reject EndScene without BeginScene") ||
        !Check(primary.SetViewport(0, 0, 0, 0, 0.f, 1.f), "Full client viewport") ||
        !Check(primary.Clear(true, true, deferred.data(), 0.25f), "Queue W3D preframe clear") ||
        !Check(primary.BeginScene(), "Begin native scene with deferred clear") ||
        !Check(primary.IsSceneOpen() && primary.Presentation().IsRecording(),
               "Active frame state"))
        return 1;
    ComPtr<ID3D12Resource> captured;
    unsigned rowPitch = 0;
    if (!Check(RecordCenterReadback(primary.Presentation(), captured, rowPitch),
               "Record deferred clear readback") ||
        !Check(primary.EndScene(true, 0), "End and present deferred clear") ||
        !Check(VerifyCenterReadback(primary.Presentation(), captured, rowPitch, deferred),
               "Deferred clear GPU pixel matches"))
        return 1;

    const std::array<float, 4> duringFrame = {{0.7f, 0.15f, 0.3f, 1.f}};
    captured.Reset();
    if (!Check(primary.SetViewport(8, 6, 128, 128, 0.f, 1.f),
               "Set diagnostic viewport before scene") ||
        !Check(!primary.SetViewport(0, 0, 0, 0, 0.9f, 0.1f),
               "Reject invalid depth viewport") ||
        !Check(primary.BeginScene(), "Begin native scene without clear") ||
        !Check(primary.SetViewport(0, 0, 0, 0, 0.f, 1.f),
               "Update live viewport") ||
        !Check(primary.Clear(true, true, duringFrame.data(), 0.6f),
               "Clear during scene") ||
        !Check(RecordCenterReadback(primary.Presentation(), captured, rowPitch),
               "Record in-scene clear readback") ||
        !Check(primary.EndScene(false, 0), "Submit without DXGI Present") ||
        !Check(!primary.IsSceneOpen() && !primary.Presentation().IsRecording(),
               "Nonflipping frame closed") ||
        !Check(VerifyCenterReadback(primary.Presentation(), captured, rowPitch, duringFrame),
               "In-scene clear GPU pixel matches"))
        return 1;

    const float blue[] = {0.02f, 0.1f, 0.35f, 1.f};
    const float green[] = {0.02f, 0.35f, 0.1f, 1.f};
    for (unsigned i = 0; i < 3; ++i)
        if (!Check(primary.PresentDiagnostic(blue), "Present to game HWND"))
            return 1;

    if (!Check(SetClientArea(game.hwnd, 320, 200, WS_OVERLAPPEDWINDOW),
               "Resize windowed client"))
        return 1;
    game.Pump();
    if (!Check(primary.Synchronize() == DX12PrimaryWindow::Status::Ready,
               "Synchronize windowed resize") ||
        !Check(primary.Presentation().Width() == 320 &&
               primary.Presentation().Height() == 200,
               "Exact windowed back-buffer size") ||
        !Check(primary.PresentDiagnostic(green), "Present resized window"))
        return 1;

    // Actual minimized windows can still report nonzero GetClientRect().
    // IsIconic must take precedence and preserve the previously allocated
    // buffers while no GPU work is submitted.
    ShowWindow(game.hwnd, SW_MINIMIZE);
    game.Pump();
    if (!Check(primary.Synchronize() == DX12PrimaryWindow::Status::Suspended,
               "Minimize suspends presentation") ||
        !Check(primary.Presentation().IsReady(), "Minimize retains buffers") ||
        !Check(!primary.PresentDiagnostic(blue), "No present while minimized"))
        return 1;

    ShowWindow(game.hwnd, SW_RESTORE);
    game.Pump();
    if (!Check(primary.Synchronize() == DX12PrimaryWindow::Status::Ready,
               "Restore presentation") ||
        !Check(MatchesClient(primary, game.hwnd), "Restore client size") ||
        !Check(primary.PresentDiagnostic(green), "Present after restore"))
        return 1;

    // DXGI remains windowed; application-owned WS_POPUP makes a borderless
    // presentation surface without a second HWND or exclusive-mode toggle.
    SetWindowLongPtrW(game.hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    if (!Check(SetWindowPos(game.hwnd, HWND_TOP, 0, 0, 480, 270,
                           SWP_FRAMECHANGED | SWP_SHOWWINDOW) != FALSE,
               "Switch to borderless style"))
        return 1;
    game.Pump();
    if (!Check(primary.IsBorderless(), "Borderless classification") ||
        !Check(primary.Synchronize() == DX12PrimaryWindow::Status::Ready,
               "Borderless resize") ||
        !Check(primary.Presentation().Width() == 480 &&
               primary.Presentation().Height() == 270,
               "Borderless client resolution") ||
        !Check(primary.PresentDiagnostic(blue), "Present borderless"))
        return 1;

    SetWindowLongPtrW(game.hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    if (!Check(SetClientArea(game.hwnd, 400, 240,
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE),
               "Restore decorated window"))
        return 1;
    game.Pump();
    if (!Check(!primary.IsBorderless(), "Windowed classification") ||
        !Check(primary.Synchronize() == DX12PrimaryWindow::Status::Ready,
               "Restore windowed swap chain") ||
        !Check(primary.Presentation().Width() == 400 &&
               primary.Presentation().Height() == 240,
               "Restored client resolution") ||
        !Check(primary.PresentDiagnostic(green), "Present windowed"))
        return 1;

    primary.Shutdown();
    if (!Check(IsWindow(game.hwnd) != FALSE, "Adapter never destroys game HWND") ||
        !Check(!primary.Presentation().IsReady(), "Shutdown releases D3D12") ||
        !Check(primary.Window() == nullptr, "Shutdown detaches HWND"))
        return 1;

    if (!Check(primary.Attach(game.hwnd, true), "Reattach same HWND") ||
        !Check(primary.PresentDiagnostic(blue), "Present after reattach"))
        return 1;
    if (!Check(DestroyWindow(game.hwnd) != FALSE, "Destroy game-owned window"))
        return 1;
    game.hwnd = nullptr;
    if (!Check(primary.Synchronize() == DX12PrimaryWindow::Status::Failed,
               "Detect destroyed native HWND") ||
        !Check(!primary.Presentation().IsReady(), "Release GPU resources on HWND loss"))
        return 1;

    primary.Shutdown();
    std::cout << "DX12 primary HWND ownership, WARP presentation, resize, "
                 "minimize/restore, borderless/windowed, teardown: PASS\n";
    return 0;
}
