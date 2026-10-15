/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/

#include "DX12W3DPassGraph.h"

namespace
{
    D3D12_RESOURCE_BARRIER ResourceTransition(ID3D12Resource * resource,
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
}

const char * DX12W3DPassName(DX12W3DPass stage)
{
    switch (stage)
    {
        case DX12W3DPass::OpaqueWorld:  return "opaque-world";
        case DX12W3DPass::Terrain:      return "terrain";
        case DX12W3DPass::Water:        return "water";
        case DX12W3DPass::Effects:      return "effects";
        case DX12W3DPass::Transparency: return "transparency";
        case DX12W3DPass::Shroud:       return "shroud";
        case DX12W3DPass::UI:           return "ui";
        case DX12W3DPass::PostProcess:  return "postprocess";
        default:                        return "invalid";
    }
}

bool DX12W3DPassGraph::Fail(const char * message)
{
    error_ = message ? message : "Invalid W3D render-pass operation";
    return false;
}

void DX12W3DPassGraph::Reset()
{
    states_.clear();
    list_ = nullptr;
    backBuffer_ = nullptr;
    depthBuffer_ = nullptr;
    currentPass_ = -1;
    barrierCount_ = 0;
    recording_ = false;
    error_.clear();
}

bool DX12W3DPassGraph::BeginFrame(ID3D12GraphicsCommandList * commandList,
                                  ID3D12Resource * primaryBackBuffer,
                                  ID3D12Resource * primaryDepth)
{
    if (recording_)
        return Fail("BeginFrame called while another pass graph is recording");
    Reset();
    if (!commandList || !primaryBackBuffer || !primaryDepth ||
        primaryBackBuffer == primaryDepth)
        return Fail("BeginFrame requires a command list and separate primary color/depth targets");

    list_ = commandList;
    backBuffer_ = primaryBackBuffer;
    depthBuffer_ = primaryDepth;
    recording_ = true;
    states_.emplace(backBuffer_, D3D12_RESOURCE_STATE_PRESENT);
    states_.emplace(depthBuffer_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    if (!RequireState(backBuffer_, D3D12_RESOURCE_STATE_RENDER_TARGET))
    {
        Reset();
        return false;
    }
    return true;
}

bool DX12W3DPassGraph::RegisterResource(ID3D12Resource * resource,
                                        D3D12_RESOURCE_STATES currentState)
{
    if (!recording_ || !resource || !list_)
        return Fail("RegisterResource requires an active frame and a non-null resource");
    const auto previous = states_.find(resource);
    if (previous != states_.end())
        return previous->second == currentState ||
               Fail("Resource was registered twice with conflicting states");
    states_.emplace(resource, currentState);
    return true;
}

bool DX12W3DPassGraph::GetTrackedState(
    ID3D12Resource * resource, D3D12_RESOURCE_STATES & out) const
{
    const auto it = states_.find(resource);
    if (it == states_.end())
        return false;
    out = it->second;
    return true;
}

bool DX12W3DPassGraph::RequireState(ID3D12Resource * resource,
                                    D3D12_RESOURCE_STATES desiredState)
{
    if (!recording_ || !list_ || !resource)
        return Fail("RequireState requires an active frame and resource");
    auto it = states_.find(resource);
    if (it == states_.end())
        return Fail("Resource must be registered with its real state before transition");
    if (it->second != desiredState)
    {
        const auto barrier = ResourceTransition(resource, it->second, desiredState);
        list_->ResourceBarrier(1, &barrier);
        it->second = desiredState;
        ++barrierCount_;
    }
    return true;
}

bool DX12W3DPassGraph::UAVBarrier(ID3D12Resource * resource)
{
    if (!recording_ || !list_ || !resource)
        return Fail("UAVBarrier requires an active frame and resource");
    const auto it = states_.find(resource);
    if (it == states_.end() || it->second != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        return Fail("UAVBarrier requires a registered resource in UAV state");
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    list_->ResourceBarrier(1, &barrier);
    ++barrierCount_;
    return true;
}

bool DX12W3DPassGraph::BeginPass(DX12W3DPass stage,
                                 const DX12W3DPassBindings & bindings)
{
    const int order = static_cast<int>(stage);
    if (!recording_ || !list_)
        return Fail("BeginPass requires an active frame");
    if (order < 0 || stage >= DX12W3DPass::Count)
        return Fail("Unknown W3D render-pass stage");
    if (order <= currentPass_)
        return Fail("W3D render passes must advance strictly in canonical order");
    if (!bindings.colorTarget && !bindings.depthTarget &&
        bindings.uavOutputCount == 0)
        return Fail("A pass must declare a color, depth or UAV output");
    if ((bindings.shaderInputCount != 0 && !bindings.shaderInputs) ||
        (bindings.uavOutputCount != 0 && !bindings.uavOutputs) ||
        (bindings.colorTarget && bindings.colorTarget == bindings.depthTarget))
        return Fail("Invalid render-pass targets or shader dependencies");

    // Validate the *entire* set before emitting any barriers, so a bad pass
    // cannot partially change the tracked state or command list.
    D3D12_RESOURCE_STATES ignored = D3D12_RESOURCE_STATE_COMMON;
    if ((bindings.colorTarget && !GetTrackedState(bindings.colorTarget, ignored)) ||
        (bindings.depthTarget && !GetTrackedState(bindings.depthTarget, ignored)))
        return Fail("Render-pass output resource is not registered");

    for (std::size_t i = 0; i < bindings.uavOutputCount; ++i)
    {
        ID3D12Resource * output = bindings.uavOutputs[i];
        if (!output || output == bindings.colorTarget ||
            output == bindings.depthTarget || !GetTrackedState(output, ignored))
            return Fail("Unregistered or conflicting UAV output");
        for (std::size_t j = 0; j < i; ++j)
            if (output == bindings.uavOutputs[j])
                return Fail("Duplicate UAV write binding in same pass");
    }
    for (std::size_t i = 0; i < bindings.shaderInputCount; ++i)
    {
        ID3D12Resource * input = bindings.shaderInputs[i];
        if (!input || input == bindings.colorTarget ||
            input == bindings.depthTarget || !GetTrackedState(input, ignored))
            return Fail("Unregistered input or same-pass read/write resource hazard");
        for (std::size_t j = 0; j < bindings.uavOutputCount; ++j)
            if (input == bindings.uavOutputs[j])
                return Fail("Simultaneous UAV write and shader-read hazard");
    }

    // Stage transitions are recorded before the caller binds descriptors and
    // emits draws. This graph deliberately does not claim actual W3D content.
    if (bindings.colorTarget &&
        !RequireState(bindings.colorTarget, D3D12_RESOURCE_STATE_RENDER_TARGET))
        return false;
    if (bindings.depthTarget &&
        !RequireState(bindings.depthTarget, D3D12_RESOURCE_STATE_DEPTH_WRITE))
        return false;
    for (std::size_t i = 0; i < bindings.shaderInputCount; ++i)
        if (!RequireState(bindings.shaderInputs[i],
                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            return false;
    for (std::size_t i = 0; i < bindings.uavOutputCount; ++i)
    {
        ID3D12Resource * output = bindings.uavOutputs[i];
        D3D12_RESOURCE_STATES previous = D3D12_RESOURCE_STATE_COMMON;
        if (!GetTrackedState(output, previous))
            return Fail("Untracked UAV write after pass validation");
        // D3D12 UAV writes need explicit memory ordering even when the resource
        // stays in UNORDERED_ACCESS across effects/shroud/postprocess passes.
        if (previous == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            if (!UAVBarrier(output))
                return false;
        }
        else if (!RequireState(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            return false;
    }

    currentPass_ = order;
    return true;
}

bool DX12W3DPassGraph::EndFrame()
{
    if (!recording_ || !list_ || !backBuffer_)
        return Fail("EndFrame requires an active render-pass graph");
    if (!RequireState(backBuffer_, D3D12_RESOURCE_STATE_PRESENT))
        return false;
    // No retained resource pointers beyond the frame. Re-register offscreen
    // resources with the *actual* final state next frame (R07 owns lifetime).
    states_.clear();
    recording_ = false;
    list_ = nullptr;
    backBuffer_ = nullptr;
    depthBuffer_ = nullptr;
    return true;
}
