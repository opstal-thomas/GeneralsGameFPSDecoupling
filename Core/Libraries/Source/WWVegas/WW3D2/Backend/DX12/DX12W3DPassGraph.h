/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/

// DX12-B05: explicit, backend-private W3D pass order and resource-state
// contract. This class does NOT issue draw calls or create W3D assets.
// Resource owners must register their actual state on each frame.
#pragma once

#include <d3d12.h>

#include <cstddef>
#include <string>
#include <unordered_map>

// Submission sequence for the future authoritative W3D renderer. Stages may
// be omitted, but must never be entered out of order within one frame.
enum class DX12W3DPass : unsigned
{
    OpaqueWorld,
    Terrain,
    Water,
    Effects,
    Transparency,
    Shroud,
    UI,
    PostProcess,
    Count
};

const char * DX12W3DPassName(DX12W3DPass stage);

// Native GPU targets/read dependencies for one pass. An output must not also
// be sampled in the same pass; use an offscreen intermediate / ping-pong.
// Caller still owns RTV/DSV/SRV descriptor binding and actual draw calls.
struct DX12W3DPassBindings
{
    ID3D12Resource * colorTarget = nullptr;
    ID3D12Resource * depthTarget = nullptr;
    ID3D12Resource * const * shaderInputs = nullptr;
    std::size_t shaderInputCount = 0;
    // Optional compute/UAV writes for effects, shroud and postprocess.
    // Same-pass SRV and UAV access to one resource is forbidden.
    ID3D12Resource * const * uavOutputs = nullptr;
    std::size_t uavOutputCount = 0;
};

class DX12W3DPassGraph final
{
public:
    // Called only after command-list Reset. The primary color buffer starts
    // in PRESENT/COMMON and the persistent depth surface in DEPTH_WRITE.
    bool BeginFrame(ID3D12GraphicsCommandList * commandList,
                    ID3D12Resource * primaryBackBuffer,
                    ID3D12Resource * primaryDepth);

    // Offscreen textures/buffers must be explicitly registered with their
    // *actual* current D3D12 state before the first use this frame.
    bool RegisterResource(ID3D12Resource * resource,
                          D3D12_RESOURCE_STATES currentState);

    // Emits a real barrier when the tracked state differs. No unknown resource
    // is transitioned based on a guessed "before" state.
    bool RequireState(ID3D12Resource * resource,
                      D3D12_RESOURCE_STATES desiredState);

    // Emits a UAV memory-ordering barrier for a registered UAV target.
    bool UAVBarrier(ID3D12Resource * resource);

    // Enforces pass order and all binding hazards before recording transitions.
    bool BeginPass(DX12W3DPass stage, const DX12W3DPassBindings & bindings);

    // Returns swapchain color to PRESENT even if the last stage is skipped.
    bool EndFrame();
    void Reset();

    bool IsRecording() const { return recording_; }
    int CurrentPassOrdinal() const { return currentPass_; }
    unsigned BarrierCount() const { return barrierCount_; }
    const char * LastError() const { return error_.c_str(); }
    bool GetTrackedState(ID3D12Resource * resource,
                         D3D12_RESOURCE_STATES & out) const;

private:
    bool Fail(const char * message);

    ID3D12GraphicsCommandList * list_ = nullptr; // borrowed per frame
    ID3D12Resource * backBuffer_ = nullptr;       // borrowed per frame
    ID3D12Resource * depthBuffer_ = nullptr;      // borrowed per frame
    std::unordered_map<ID3D12Resource *, D3D12_RESOURCE_STATES> states_;
    int currentPass_ = -1;
    unsigned barrierCount_ = 0;
    bool recording_ = false;
    std::string error_;
};
