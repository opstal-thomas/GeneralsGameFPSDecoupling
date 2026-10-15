/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License, version 3 or later.
*/

// Internal DX12 W3D presentation infrastructure. Do not include this header
// from gameplay; Renderer.h remains the backend-neutral integration boundary.
#pragma once

#include <memory>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

class DX12PresentationDevice final
{
public:
    DX12PresentationDevice();
    ~DX12PresentationDevice();

    DX12PresentationDevice(const DX12PresentationDevice &) = delete;
    DX12PresentationDevice & operator=(const DX12PresentationDevice &) = delete;

    // nativeWindow is an HWND. WARP is explicit for deterministic CI testing;
    // the production path selects a physical adapter instead.
    bool Initialize(void * nativeWindow, unsigned width, unsigned height, bool useWarp);
    void Shutdown();

    // Begins recording commands into the current swap-chain back buffer.
    // Future DX12 W3D draws can append commands through ActiveCommandList().
    // The back buffer must be in RENDER_TARGET state when EndFrame is called.
    bool BeginFrame(const float clearColor[4]);
    bool EndFrame(unsigned syncInterval);
    // Record an in-frame clear on the active game back buffer. Unlike BeginFrame,
    // this can be invoked after other frame state has been established.
    bool ClearActiveFrame(bool color, bool depth, const float rgba[4], float z);
    bool WaitForIdle();
    bool Resize(unsigned width, unsigned height);

    // Size of the original DX8 camera viewport, used to preserve projection
    // geometry when the separately resizable DX12 window has another aspect.
    void SetSourceViewport(unsigned sourceWidth, unsigned sourceHeight);
    float SourceAspect() const;

    bool IsReady() const;
    unsigned Width() const;
    unsigned Height() const;
    const char * LastError() const;

    // Backend-private native accessors; never surface through Renderer.h.
    ID3D12Device * NativeDevice() const;
    ID3D12GraphicsCommandList * ActiveCommandList() const;
    ID3D12Resource * CurrentBackBuffer() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
