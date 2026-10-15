/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/

// DX12-B03: exclusive DX12 presentation on the game's BORROWED primary HWND.
// The application owns its window procedure, window style and message pump.
// This class never creates, destroys, subclasses or moves the HWND.
#pragma once

#include "DX12PresentationDevice.h"
#include <string>

class DX12PrimaryWindow final
{
public:
    enum class Status { Unavailable, Ready, Suspended, Failed };

    DX12PrimaryWindow() = default;
    DX12PrimaryWindow(const DX12PrimaryWindow &) = delete;
    DX12PrimaryWindow & operator=(const DX12PrimaryWindow &) = delete;
    ~DX12PrimaryWindow() { Shutdown(); }

    // Accept only a root/top-level HWND created by this process. No D3D8
    // device is needed. WARP is for deterministic smoke tests and opt-in dev.
    bool Attach(void * applicationWindow, bool useWarp);
    // Poll the actual client area after application message dispatch. A
    // minimized/zero-sized window is suspended, not a resize failure.
    Status Synchronize();
    // Diagnostic only: B04 will own the normal W3D Begin/End frame lifecycle.
    bool PresentDiagnostic(const float clearColor[4]);
    void Shutdown();

    Status CurrentStatus() const { return status_; }
    void * Window() const { return window_; }
    bool IsBorderless() const;
    const char * LastError() const { return error_.c_str(); }
    DX12PresentationDevice & Presentation() { return device_; }
    const DX12PresentationDevice & Presentation() const { return device_; }

private:
    Status Fail(const char * message);
    void * window_ = nullptr;
    bool warp_ = false;
    Status status_ = Status::Unavailable;
    DX12PresentationDevice device_;
    std::string error_;
};
