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
#include <iostream>

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
