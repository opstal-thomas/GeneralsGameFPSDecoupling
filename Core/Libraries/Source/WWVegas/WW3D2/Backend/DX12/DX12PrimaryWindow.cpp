/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#include "DX12PrimaryWindow.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

DX12PrimaryWindow::Status DX12PrimaryWindow::Fail(const char * message)
{
    error_ = message ? message : "DX12 primary-window presentation error";
    status_ = Status::Failed;
    device_.Shutdown();
    return status_;
}

bool DX12PrimaryWindow::Attach(void * applicationWindow, bool useWarp)
{
    Shutdown();
    HWND hwnd = static_cast<HWND>(applicationWindow);
    if (!hwnd || !IsWindow(hwnd))
    {
        Fail("Invalid application HWND");
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != GetCurrentProcessId() || GetAncestor(hwnd, GA_ROOT) != hwnd)
    {
        Fail("Expected a top-level HWND owned by the game process");
        return false;
    }
    window_ = applicationWindow; // borrowed, never DestroyWindow()
    warp_ = useWarp;
    // A hidden or minimized game window may not have a drawable client area
    // at startup. Keep the HWND and wait for the game to size/show it.
    return Synchronize() != Status::Failed;
}

DX12PrimaryWindow::Status DX12PrimaryWindow::Synchronize()
{
    HWND hwnd = static_cast<HWND>(window_);
    if (!hwnd)
        return Fail("No primary game window attached");
    if (!IsWindow(hwnd))
        return Fail("Primary game window was destroyed");

    if (IsIconic(hwnd))
    {
        status_ = Status::Suspended;
        return status_;
    }

    RECT client = {};
    if (!GetClientRect(hwnd, &client))
        return Fail("GetClientRect failed for primary game window");
    const LONG width = client.right - client.left;
    const LONG height = client.bottom - client.top;
    if (width <= 0 || height <= 0)
    {
        status_ = Status::Suspended;
        return status_;
    }

    const unsigned clientWidth = static_cast<unsigned>(width);
    const unsigned clientHeight = static_cast<unsigned>(height);
    if (!device_.IsReady())
    {
        if (!device_.Initialize(hwnd, clientWidth, clientHeight, warp_))
        {
            const std::string error = device_.LastError();
            return Fail(("Initialize primary DX12 swap chain: " + error).c_str());
        }
    }
    else if (clientWidth != device_.Width() || clientHeight != device_.Height())
    {
        // ResizeBuffers waits for the GPU, recreates the depth surface and
        // retains the same native HWND. Window styles remain the app's job.
        if (!device_.Resize(clientWidth, clientHeight))
        {
            const std::string error = device_.LastError();
            return Fail(("Resize primary DX12 swap chain: " + error).c_str());
        }
    }

    status_ = Status::Ready;
    return status_;
}

bool DX12PrimaryWindow::PresentDiagnostic(const float clearColor[4])
{
    if (!clearColor || Synchronize() != Status::Ready)
        return false;
    if (!device_.BeginFrame(clearColor) || !device_.EndFrame(0))
    {
        const std::string error = device_.LastError();
        Fail(("Present diagnostic frame on primary HWND: " + error).c_str());
        return false;
    }
    return true;
}

bool DX12PrimaryWindow::IsBorderless() const
{
    HWND hwnd = static_cast<HWND>(window_);
    if (!hwnd || !IsWindow(hwnd))
        return false;
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    return (style & WS_POPUP) != 0 && (style & WS_CAPTION) == 0;
}

void DX12PrimaryWindow::Shutdown()
{
    device_.Shutdown();
    window_ = nullptr;
    warp_ = false;
    status_ = Status::Unavailable;
    error_.clear();
}
