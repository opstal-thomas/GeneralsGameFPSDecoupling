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

    // B04: W3D clears *before* Begin_Scene, unlike the D3D12 recording
    // contract. Queue them until BeginScene, or emit them during an open scene.
    bool Clear(bool color, bool depth, const float rgba[4], float z);
    bool SetViewport(unsigned x, unsigned y, unsigned width, unsigned height,
                     float minDepth, float maxDepth);
    bool BeginScene();
    // R07 returns from offscreen viewport dimensions to the game's saved
    // primary camera viewport/scissor, rather than always using full screen.
    bool RestorePrimaryViewport() { return sceneOpen_ && ApplyViewport(); }
    // Native backend integrations can advance to later W3D render passes.
    // Rendering objects/assets themselves remains R01/M01/B07 work.
    bool BeginPass(DX12W3DPass stage, const DX12W3DPassBindings & bindings);
    bool EndScene(bool flipFrame, unsigned syncInterval);
    bool IsSceneOpen() const { return sceneOpen_; }
    void Shutdown();

    Status CurrentStatus() const { return status_; }
    void * Window() const { return window_; }
    bool IsBorderless() const;
    const char * LastError() const { return error_.c_str(); }
    DX12PresentationDevice & Presentation() { return device_; }
    const DX12PresentationDevice & Presentation() const { return device_; }

private:
    Status Fail(const char * message);
    bool ApplyViewport();
    void * window_ = nullptr;
    bool warp_ = false;
    Status status_ = Status::Unavailable;
    DX12PresentationDevice device_;
    bool sceneOpen_ = false;
    bool clearColor_ = false;
    bool clearDepth_ = false;
    float rgba_[4] = {0.f, 0.f, 0.f, 1.f};
    float clearZ_ = 1.f;
    unsigned viewportX_ = 0, viewportY_ = 0;
    unsigned viewportWidth_ = 0, viewportHeight_ = 0;
    float minDepth_ = 0.f, maxDepth_ = 1.f;
    std::string error_;
};
