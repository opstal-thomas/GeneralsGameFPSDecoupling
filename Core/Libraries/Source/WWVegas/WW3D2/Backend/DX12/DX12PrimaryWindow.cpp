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
#include <d3d12.h>
#include <algorithm>
#include <cstring>

DX12PrimaryWindow::Status DX12PrimaryWindow::Fail(const char * message)
{
    error_ = message ? message : "DX12 primary-window presentation error";
    status_ = Status::Failed;
    sceneOpen_ = false;
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
    if (status_ == Status::Failed)
        return status_; // require explicit Shutdown/Attach after terminal failure
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

bool DX12PrimaryWindow::Clear(bool color, bool depth, const float rgba[4], float z)
{
    if ((!rgba && color) || (depth && !(z >= 0.f && z <= 1.f)))
    {
        error_ = "Invalid frame clear parameters";
        return false;
    }
    if (!color && !depth)
        return true;
    if (sceneOpen_)
    {
        if (!device_.ClearActiveFrame(color, depth, rgba, z))
        {
            const std::string error = device_.LastError();
            Fail(("Native DX12 clear: " + error).c_str());
            return false;
        }
        return true;
    }
    if (!window_ || status_ == Status::Failed)
    {
        error_ = "No active DX12 primary window for deferred clear";
        return false;
    }
    if (color)
    {
        std::memcpy(rgba_, rgba, sizeof(rgba_));
        clearColor_ = true;
    }
    if (depth)
    {
        clearZ_ = z;
        clearDepth_ = true;
    }
    return true;
}

bool DX12PrimaryWindow::ApplyViewport()
{
    if (!sceneOpen_)
        return true;
    const unsigned targetWidth = device_.Width();
    const unsigned targetHeight = device_.Height();
    const unsigned x = (std::min)(viewportX_, targetWidth);
    const unsigned y = (std::min)(viewportY_, targetHeight);
    const unsigned width = viewportWidth_ ?
        (std::min)(viewportWidth_, targetWidth - x) : targetWidth - x;
    const unsigned height = viewportHeight_ ?
        (std::min)(viewportHeight_, targetHeight - y) : targetHeight - y;
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = static_cast<float>(x);
    viewport.TopLeftY = static_cast<float>(y);
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = minDepth_;
    viewport.MaxDepth = maxDepth_;
    D3D12_RECT scissor = {static_cast<LONG>(x), static_cast<LONG>(y),
                          static_cast<LONG>(x + width), static_cast<LONG>(y + height)};
    ID3D12GraphicsCommandList * list = device_.ActiveCommandList();
    if (!list)
    {
        Fail("No active DX12 command list for viewport");
        return false;
    }
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    return true;
}

bool DX12PrimaryWindow::SetViewport(unsigned x, unsigned y, unsigned width,
                                     unsigned height, float minDepth, float maxDepth)
{
    if (!(minDepth >= 0.f && maxDepth <= 1.f && minDepth <= maxDepth))
    {
        error_ = "Invalid viewport depth range";
        return false;
    }
    viewportX_ = x;
    viewportY_ = y;
    viewportWidth_ = width;
    viewportHeight_ = height;
    minDepth_ = minDepth;
    maxDepth_ = maxDepth;
    return ApplyViewport();
}

bool DX12PrimaryWindow::BeginScene()
{
    if (sceneOpen_ || status_ == Status::Failed)
        return false;
    if (Synchronize() != Status::Ready)
        return false; // minimized/zero-sized HWND: no present, no GPU failure
    if (!device_.BeginFrame(rgba_, clearColor_, clearDepth_, clearZ_))
    {
        const std::string error = device_.LastError();
        Fail(("Begin native DX12 game frame: " + error).c_str());
        return false;
    }
    sceneOpen_ = true;
    clearColor_ = clearDepth_ = false;
    return ApplyViewport();
}

bool DX12PrimaryWindow::EndScene(bool flipFrame, unsigned syncInterval)
{
    if (!sceneOpen_)
        return false;
    sceneOpen_ = false;
    if (!device_.EndFrame(syncInterval, flipFrame))
    {
        const std::string error = device_.LastError();
        Fail(("End native DX12 game frame: " + error).c_str());
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
    sceneOpen_ = clearColor_ = clearDepth_ = false;
    rgba_[0] = rgba_[1] = rgba_[2] = 0.f;
    rgba_[3] = clearZ_ = 1.f;
    viewportX_ = viewportY_ = viewportWidth_ = viewportHeight_ = 0;
    minDepth_ = 0.f;
    maxDepth_ = 1.f;
    window_ = nullptr;
    warp_ = false;
    status_ = Status::Unavailable;
    error_.clear();
}
