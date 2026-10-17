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

#include "DX12W3DPassGraph.h"
#include <memory>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct IUnknown;

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
    // Clears are optional because W3D may request only depth, only color or
    // neither. W3D queues Clear before Begin_Scene.
    bool BeginFrame(const float clearColor[4], bool clearColorTarget = true,
                    bool clearDepthTarget = true, float clearDepthValue = 1.f);
    // A non-flipping End_Scene must submit the command list without Present.
    bool EndFrame(unsigned syncInterval, bool present = true);
    // Record an in-frame clear on the active game back buffer. Unlike BeginFrame,
    // this can be invoked after other frame state has been established.
    bool ClearActiveFrame(bool color, bool depth, const float rgba[4], float z);

    // B05: resource transitions and pass scheduling on the *same* real
    // command list owned by this frame; future native draw paths call these.
    bool RegisterFrameResource(ID3D12Resource * resource,
                               D3D12_RESOURCE_STATES state);
    bool TransitionFrameResource(ID3D12Resource * resource,
                                 D3D12_RESOURCE_STATES desired);
    bool BarrierFrameUAV(ID3D12Resource * resource);
    bool BeginRenderPass(DX12W3DPass stage,
                         const DX12W3DPassBindings & bindings);
    int CurrentRenderPassOrdinal() const;
    unsigned FrameBarrierCount() const;
    bool GetFrameResourceState(ID3D12Resource * resource,
                               D3D12_RESOURCE_STATES & result) const;

    // Keep resources referenced by the currently recording command list
    // alive until its signalled GPU fence completes. R01 relies on this for
    // buffer generations and upload staging, including released handles.
    bool RetainFrameResource(ID3D12Resource * resource);
    // Shader-visible descriptor heaps are COM objects, not D3D12 resources.
    // Retain them through the identical GPU fence as uploaded textures so
    // root descriptor tables never point into freed/reused descriptor memory.
    bool RetainFrameObject(IUnknown * object);
    unsigned InFlightResourceCount() const;
    unsigned InFlightObjectCount() const;
    // Incremented at every successful BeginFrame, including after reset.
    // Descriptor caches can never reuse tables recorded for an older list.
    unsigned long long RecordingSerial() const;
    unsigned long long CompletedFenceValue() const;
    unsigned long long LastSubmittedFenceValue() const;
    // R02: one per-presentation pool, shared between vertex and index uploads.
    // Returns a borrowed resource (valid while this device lives); callers
    // retain the resulting slice through RetainFrameResource for submitted work.
    bool ReserveTransientGeometry(unsigned bytes, ID3D12Resource * & resource, unsigned & offset);
    unsigned TransientGeometryPageCount() const;
    unsigned long long TransientGeometryResidentBytes() const;
    // DX8 SetIndices carried a base-vertex offset. D3D12 applies it during
    // DrawIndexedInstanced, not IASetIndexBuffer; cache for B07 submission.
    void SetBoundBaseVertexIndex(unsigned index);
    unsigned BoundBaseVertexIndex() const;
    // R08: submit recorded draws/copies, wait for the readback fence and
    // reopen the same frame. Keep B05 tracked resource states and ordinal;
    // consumers must rebind OM and viewport (command-list Reset clears RS).
    // This intentionally stalls only explicit synchronous readback callers.
    bool FlushAndContinue();
    // IA buffer views survive the R08 synchronous readback submission's
    // command-list reset; callers need not rebind the same W3D geometry.
    bool TrackVertexBinding(unsigned stream, const D3D12_VERTEX_BUFFER_VIEW * view);
    bool TrackIndexBinding(const D3D12_INDEX_BUFFER_VIEW * view);
    bool WaitForIdle();
    bool Resize(unsigned width, unsigned height);

    // Size of the original DX8 camera viewport, used to preserve projection
    // geometry when the separately resizable DX12 window has another aspect.
    void SetSourceViewport(unsigned sourceWidth, unsigned sourceHeight);
    float SourceAspect() const;

    bool IsReady() const;
    bool IsRecording() const;
    bool IsDeviceHealthy() const;
    bool GetAdapterIdentity(unsigned & vendorId, unsigned & deviceId,
                            unsigned long long & driverVersion) const;
    unsigned Width() const;
    unsigned Height() const;
    const char * LastError() const;

    // Backend-private native accessors; never surface through Renderer.h.
    ID3D12Device * NativeDevice() const;
    ID3D12GraphicsCommandList * ActiveCommandList() const;
    ID3D12Resource * CurrentBackBuffer() const;
    ID3D12Resource * CurrentDepthTarget() const;
    // The current primary descriptors are valid only during this recording.
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentDepthDSV() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
