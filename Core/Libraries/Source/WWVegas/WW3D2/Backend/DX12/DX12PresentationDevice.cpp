/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License, version 3 or later.
*/

#include "DX12PresentationDevice.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <deque>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr UINT BufferCount = 2;
    constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT DepthBufferFormat = DXGI_FORMAT_D32_FLOAT;
    constexpr DWORD FenceTimeoutMs = 30000;

}

struct DX12PresentationDevice::Impl
{
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource> depthBuffer;
    std::array<ComPtr<ID3D12Resource>, BufferCount> buffers;
    std::array<ComPtr<ID3D12CommandAllocator>, BufferCount> allocators;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    std::array<UINT64, BufferCount> frameFences = {};
    UINT64 nextFence = 1;
    unsigned long long recordingSerial = 0;
    UINT rtvIncrement = 0;
    UINT currentBuffer = 0;
    unsigned width = 0;
    unsigned height = 0;
    float sourceAspect = 0.f;
    bool recording = false;
    unsigned boundBaseVertexIndex = 0;
    // Resources referenced by a submitted command list cannot be released
    // when their W3D owner is destroyed on the CPU. Keep a per-frame
    // AddRef until the queue signals its fence; handles may be reused.
    std::vector<ComPtr<ID3D12Resource>> recordingResources;
    std::deque<std::pair<UINT64, std::vector<ComPtr<ID3D12Resource>>>> retiredResources;
    std::vector<ComPtr<IUnknown>> recordingObjects;
    std::deque<std::pair<UINT64, std::vector<ComPtr<IUnknown>>>> retiredObjects;
    void CollectRetired()
    {
        if (!fence)
            return;
        const UINT64 done = fence->GetCompletedValue();
        if (done == UINT64_MAX)
            return;
        while (!retiredResources.empty() && retiredResources.front().first <= done)
            retiredResources.pop_front();
        while (!retiredObjects.empty() && retiredObjects.front().first <= done)
            retiredObjects.pop_front();
    }
    DX12W3DPassGraph passGraph;
    std::string lastError;

    bool Fail(const char * operation, HRESULT hr)
    {
        char code[16];
        std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(hr));
        lastError = std::string(operation) + ": " + code;
        if (device && FAILED(device->GetDeviceRemovedReason()))
        {
            const HRESULT reason = device->GetDeviceRemovedReason();
            std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(reason));
            lastError += std::string(" (device removed: ") + code + ")";
        }
        return false;
    }

    bool Check(HRESULT hr, const char * operation)
    {
        return SUCCEEDED(hr) || Fail(operation, hr);
    }

    bool WaitForFence(UINT64 value)
    {
        if (value == 0)
            return true;
        if (!fence || !fenceEvent)
            return Fail("Fence not initialized", E_FAIL);

        const UINT64 completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX)
            return Fail("GPU fence/device failure", DXGI_ERROR_DEVICE_REMOVED);
        if (completed < value)
        {
            if (!Check(fence->SetEventOnCompletion(value, fenceEvent),
                       "ID3D12Fence::SetEventOnCompletion"))
                return false;
            const DWORD wait = WaitForSingleObject(fenceEvent, FenceTimeoutMs);
            if (wait != WAIT_OBJECT_0)
                return Fail("Waiting for GPU fence", HRESULT_FROM_WIN32(
                    wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError()));
        }
        return device ? Check(device->GetDeviceRemovedReason(), "DX12 device health") : true;
    }

    bool WaitForIdle()
    {
        if (!queue || !fence)
            return true;
        const UINT64 value = nextFence++;
        if (!Check(queue->Signal(fence.Get(), value), "ID3D12CommandQueue::Signal"))
            return false;
        return WaitForFence(value);
    }

    bool CreateRenderTargets()
    {
        for (UINT i = 0; i < BufferCount; ++i)
        {
            if (!Check(swapChain->GetBuffer(i, IID_PPV_ARGS(&buffers[i])),
                       "IDXGISwapChain::GetBuffer"))
                return false;
            D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(i) * rtvIncrement;
            device->CreateRenderTargetView(buffers[i].Get(), nullptr, handle);
        }
        frameFences.fill(0);
        currentBuffer = swapChain->GetCurrentBackBufferIndex();
        return true;
    }

    bool CreateDepthTarget(unsigned w, unsigned h)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = w;
        desc.Height = h;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DepthBufferFormat;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_CLEAR_VALUE clear = {};
        clear.Format = DepthBufferFormat;
        clear.DepthStencil.Depth = 1.f;
        if (!Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                       &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                       IID_PPV_ARGS(&depthBuffer)), "Create D32 depth target"))
            return false;
        D3D12_DEPTH_STENCIL_VIEW_DESC view = {};
        view.Format = DepthBufferFormat;
        view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(depthBuffer.Get(), &view,
                                      dsvHeap->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    void ReleaseAll()
    {
        for (auto & buffer : buffers)
            buffer.Reset();
        for (auto & allocator : allocators)
            allocator.Reset();
        commandList.Reset();
        depthBuffer.Reset();
        dsvHeap.Reset();
        rtvHeap.Reset();
        swapChain.Reset();
        if (fenceEvent)
        {
            CloseHandle(fenceEvent);
            fenceEvent = nullptr;
        }
        fence.Reset();
        queue.Reset();
        device.Reset();
        adapter.Reset();
        factory.Reset();
        frameFences.fill(0);
        nextFence = 1;
        currentBuffer = 0;
        rtvIncrement = 0;
        width = height = 0;
        sourceAspect = 0.f;
        recording = false;
        boundBaseVertexIndex = 0;
        recordingResources.clear();
        retiredResources.clear();
        recordingObjects.clear();
        retiredObjects.clear();
        passGraph.Reset();
    }
};

DX12PresentationDevice::DX12PresentationDevice() : impl_(new Impl)
{
}

DX12PresentationDevice::~DX12PresentationDevice()
{
    Shutdown();
}

bool DX12PresentationDevice::Initialize(void * nativeWindow, unsigned width,
                                        unsigned height, bool useWarp)
{
    Shutdown();
    Impl & state = *impl_;
    state.lastError.clear();

    HWND window = static_cast<HWND>(nativeWindow);
    if (!window || !IsWindow(window) || width == 0 || height == 0)
        return state.Fail("Invalid DX12 presentation window or dimensions", E_INVALIDARG);

    // Failure at any stage releases partially constructed COM resources.
    // The diagnostic string remains accessible after Shutdown().
    bool initialized = false;
    do
    {
        if (!state.Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&state.factory)),
                         "CreateDXGIFactory2"))
            break;

        if (useWarp)
        {
            if (!state.Check(state.factory->EnumWarpAdapter(IID_PPV_ARGS(&state.adapter)),
                             "IDXGIFactory4::EnumWarpAdapter"))
                break;
        }
        else
        {
            for (UINT i = 0; ; ++i)
            {
                ComPtr<IDXGIAdapter1> candidate;
                const HRESULT enumerated = state.factory->EnumAdapters1(i, &candidate);
                if (enumerated == DXGI_ERROR_NOT_FOUND)
                    break;
                if (!state.Check(enumerated, "IDXGIFactory::EnumAdapters1"))
                    break;
                DXGI_ADAPTER_DESC1 desc = {};
                if (FAILED(candidate->GetDesc1(&desc)) ||
                    (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                    continue;
                // Probe support without creating an extra persistent device.
                if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                                                __uuidof(ID3D12Device), nullptr)))
                {
                    state.adapter = candidate;
                    break;
                }
            }
            if (!state.adapter)
            {
                if (state.lastError.empty())
                    state.Fail("No hardware D3D12 adapter found", DXGI_ERROR_NOT_FOUND);
                break;
            }
        }

        if (!state.Check(D3D12CreateDevice(state.adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                           IID_PPV_ARGS(&state.device)), "D3D12CreateDevice"))
            break;

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (!state.Check(state.device->CreateCommandQueue(&queueDesc,
                          IID_PPV_ARGS(&state.queue)), "ID3D12Device::CreateCommandQueue"))
            break;

        DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
        swapDesc.Width = width;
        swapDesc.Height = height;
        swapDesc.Format = BackBufferFormat;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = BufferCount;
        swapDesc.Scaling = DXGI_SCALING_STRETCH;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> swapChain1;
        if (!state.Check(state.factory->CreateSwapChainForHwnd(state.queue.Get(), window,
                          &swapDesc, nullptr, nullptr, &swapChain1),
                         "IDXGIFactory4::CreateSwapChainForHwnd"))
            break;
        if (!state.Check(swapChain1.As(&state.swapChain),
                         "IDXGISwapChain1::QueryInterface IDXGISwapChain3"))
            break;
        // The client handles Alt+Enter/fullscreen policy during migration.
        state.factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = BufferCount;
        if (!state.Check(state.device->CreateDescriptorHeap(&rtvDesc,
                          IID_PPV_ARGS(&state.rtvHeap)), "Create RTV descriptor heap"))
            break;
        state.rtvIncrement = state.device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        if (!state.CreateRenderTargets())
            break;
        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
        dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvDesc.NumDescriptors = 1;
        if (!state.Check(state.device->CreateDescriptorHeap(&dsvDesc,
                          IID_PPV_ARGS(&state.dsvHeap)), "Create depth view heap") ||
            !state.CreateDepthTarget(width, height))
            break;

        bool allocatorsReady = true;
        for (auto & allocator : state.allocators)
        {
            if (!state.Check(state.device->CreateCommandAllocator(
                              D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                             "Create command allocator"))
            {
                allocatorsReady = false;
                break;
            }
        }
        if (!allocatorsReady)
            break;
        if (!state.Check(state.device->CreateCommandList(0,
                          D3D12_COMMAND_LIST_TYPE_DIRECT, state.allocators[0].Get(), nullptr,
                          IID_PPV_ARGS(&state.commandList)), "Create graphics command list"))
            break;
        if (!state.Check(state.commandList->Close(), "Close initial command list"))
            break;
        if (!state.Check(state.device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                          IID_PPV_ARGS(&state.fence)), "Create DX12 fence"))
            break;
        state.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!state.fenceEvent)
        {
            state.Fail("CreateEventW", HRESULT_FROM_WIN32(GetLastError()));
            break;
        }

        state.width = width;
        state.height = height;
        initialized = true;
    } while (false);

    if (!initialized)
        Shutdown();
    return initialized;
}

bool DX12PresentationDevice::IsReady() const
{
    return impl_->device && impl_->swapChain && impl_->depthBuffer &&
           impl_->buffers[0] && impl_->buffers[1];
}

bool DX12PresentationDevice::BeginFrame(const float clearColor[4], bool clearColorTarget,
                                        bool clearDepthTarget, float clearDepthValue)
{
    Impl & state = *impl_;
    if (!IsReady() || state.recording || (clearColorTarget && !clearColor) ||
        (clearDepthTarget && !(clearDepthValue >= 0.f && clearDepthValue <= 1.f)))
        return state.Fail("BeginFrame invalid state", E_INVALIDARG);

    state.currentBuffer = state.swapChain->GetCurrentBackBufferIndex();
    if (state.currentBuffer >= BufferCount)
        return state.Fail("Invalid swap-chain buffer index", E_FAIL);

    const UINT index = state.currentBuffer;
    if (!state.WaitForFence(state.frameFences[index]))
        return false;
    state.CollectRetired();
    state.boundBaseVertexIndex = 0;
    if (!state.recordingResources.empty() || !state.recordingObjects.empty())
        return state.Fail("Unsubmitted frame resource/descriptor retention remained", E_FAIL);
    if (!state.Check(state.allocators[index]->Reset(), "Reset frame command allocator"))
        return false;
    if (!state.Check(state.commandList->Reset(state.allocators[index].Get(), nullptr),
                     "Reset frame command list"))
        return false;

    // The B05 graph owns the back-buffer transition for this command list.
    // It also knows the persistent depth target's initial DEPTH_WRITE state.
    if (!state.passGraph.BeginFrame(state.commandList.Get(),
                                    state.buffers[index].Get(),
                                    state.depthBuffer.Get()))
    {
        const std::string error = state.passGraph.LastError();
        state.commandList->Close();
        return state.Fail(("Begin DX12 W3D pass graph: " + error).c_str(), E_FAIL);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = state.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(index) * state.rtvIncrement;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        state.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    state.commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    if (clearColorTarget)
        state.commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    if (clearDepthTarget)
        state.commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH,
                                                 clearDepthValue, 0, 0, nullptr);
    state.recording = true;
    ++state.recordingSerial;
    return true;
}

bool DX12PresentationDevice::ClearActiveFrame(bool color, bool depth, const float rgba[4], float z)
{
    Impl & state = *impl_;
    if (!state.recording || (color && !rgba) ||
        (depth && !(z >= 0.f && z <= 1.f)))
        return state.Fail("ClearActiveFrame invalid state", E_INVALIDARG);

    if (color && !state.passGraph.RequireState(
                       state.buffers[state.currentBuffer].Get(),
                       D3D12_RESOURCE_STATE_RENDER_TARGET))
        return state.Fail(state.passGraph.LastError(), E_INVALIDARG);
    if (depth && !state.passGraph.RequireState(
                       state.depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE))
        return state.Fail(state.passGraph.LastError(), E_INVALIDARG);

    if (color)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = state.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(state.currentBuffer) * state.rtvIncrement;
        state.commandList->ClearRenderTargetView(rtv, rgba, 0, nullptr);
    }
    if (depth)
        state.commandList->ClearDepthStencilView(
            state.dsvHeap->GetCPUDescriptorHandleForHeapStart(),
            D3D12_CLEAR_FLAG_DEPTH, z, 0, 0, nullptr);
    return true;
}

bool DX12PresentationDevice::EndFrame(unsigned syncInterval, bool present)
{
    Impl & state = *impl_;
    if (!state.recording || syncInterval > 4)
        return state.Fail("EndFrame invalid state or sync interval", E_INVALIDARG);

    // Always return to PRESENT from the graph's *actual* tracked state,
    // including frames with offscreen passes or temporary COPY_SOURCE reads.
    if (!state.passGraph.EndFrame())
        return state.Fail(state.passGraph.LastError(), E_INVALIDARG);
    state.recording = false;
    if (!state.Check(state.commandList->Close(), "Close frame command list"))
        return false;

    ID3D12CommandList * lists[] = {state.commandList.Get()};
    state.queue->ExecuteCommandLists(1, lists);
    const HRESULT presented = present ? state.swapChain->Present(syncInterval, 0) : S_OK;
    const UINT64 signalValue = state.nextFence++;
    const HRESULT signalled = state.queue->Signal(state.fence.Get(), signalValue);
    if (!state.Check(signalled, "Signal frame fence"))
    {
        // ExecuteCommandLists has already queued GPU references; preserve
        // these until shutdown/device loss, even when queue signaling fails.
        state.retiredResources.emplace_back(UINT64_MAX,
            std::move(state.recordingResources));
        state.retiredObjects.emplace_back(UINT64_MAX,
            std::move(state.recordingObjects));
        state.recordingResources.clear();
        state.recordingObjects.clear();
        return false;
    }
    state.frameFences[state.currentBuffer] = signalValue;
    if (!state.recordingResources.empty())
    {
        state.retiredResources.emplace_back(
            signalValue, std::move(state.recordingResources));
        state.recordingResources.clear();
    }
    if (!state.recordingObjects.empty())
    {
        state.retiredObjects.emplace_back(
            signalValue, std::move(state.recordingObjects));
        state.recordingObjects.clear();
    }
    state.CollectRetired();
    return state.Check(presented, "IDXGISwapChain::Present");
}

bool DX12PresentationDevice::RegisterFrameResource(
    ID3D12Resource * resource, D3D12_RESOURCE_STATES initial)
{
    Impl & state = *impl_;
    if (!state.recording || !state.passGraph.RegisterResource(resource, initial))
        return state.Fail(state.passGraph.LastError(), E_INVALIDARG);
    return true;
}

bool DX12PresentationDevice::TransitionFrameResource(
    ID3D12Resource * resource, D3D12_RESOURCE_STATES desired)
{
    Impl & state = *impl_;
    if (!state.recording || !state.passGraph.RequireState(resource, desired))
        return state.Fail(state.passGraph.LastError(), E_INVALIDARG);
    return true;
}

bool DX12PresentationDevice::BarrierFrameUAV(ID3D12Resource * resource)
{
    Impl & state = *impl_;
    if (!state.recording || !state.passGraph.UAVBarrier(resource))
        return state.Fail(state.passGraph.LastError(), E_INVALIDARG);
    return true;
}

bool DX12PresentationDevice::BeginRenderPass(
    DX12W3DPass stage, const DX12W3DPassBindings & bindings)
{
    Impl & state = *impl_;
    if (!state.recording || !state.passGraph.BeginPass(stage, bindings))
        return state.Fail(state.passGraph.LastError(), E_INVALIDARG);
    return true;
}

int DX12PresentationDevice::CurrentRenderPassOrdinal() const
{
    return impl_->passGraph.CurrentPassOrdinal();
}

unsigned DX12PresentationDevice::FrameBarrierCount() const
{
    return impl_->passGraph.BarrierCount();
}

bool DX12PresentationDevice::GetFrameResourceState(
    ID3D12Resource * resource, D3D12_RESOURCE_STATES & result) const
{
    return impl_->passGraph.GetTrackedState(resource, result);
}

bool DX12PresentationDevice::IsRecording() const { return impl_->recording; }

bool DX12PresentationDevice::IsDeviceHealthy() const
{
    return IsReady() && SUCCEEDED(impl_->device->GetDeviceRemovedReason());
}

bool DX12PresentationDevice::GetAdapterIdentity(
    unsigned & vendorId, unsigned & deviceId, unsigned long long & driverVersion) const
{
    vendorId = deviceId = 0;
    driverVersion = 0;
    if (!IsDeviceHealthy() || !impl_->adapter)
        return false;
    DXGI_ADAPTER_DESC1 desc = {};
    if (FAILED(impl_->adapter->GetDesc1(&desc)))
        return false;
    vendorId = desc.VendorId;
    deviceId = desc.DeviceId;
    return true;
}

bool DX12PresentationDevice::RetainFrameResource(ID3D12Resource * resource)
{
    Impl & state = *impl_;
    if (!state.recording || !resource)
        return state.Fail("RetainFrameResource requires active recording and resource", E_INVALIDARG);
    state.recordingResources.emplace_back(resource);
    return true;
}

bool DX12PresentationDevice::RetainFrameObject(IUnknown * object)
{
    Impl & state = *impl_;
    if (!state.recording || !object)
        return state.Fail("RetainFrameObject requires active frame and COM object", E_INVALIDARG);
    state.recordingObjects.emplace_back(object);
    return true;
}

unsigned long long DX12PresentationDevice::RecordingSerial() const
{
    return impl_->recording ? impl_->recordingSerial : 0;
}

unsigned DX12PresentationDevice::InFlightObjectCount() const
{
    std::size_t count = impl_->recordingObjects.size();
    for (const auto & frame : impl_->retiredObjects)
        count += frame.second.size();
    return static_cast<unsigned>(count);
}

unsigned DX12PresentationDevice::InFlightResourceCount() const
{
    std::size_t count = impl_->recordingResources.size();
    for (const auto & frame : impl_->retiredResources)
        count += frame.second.size();
    return static_cast<unsigned>(count);
}

void DX12PresentationDevice::SetBoundBaseVertexIndex(unsigned index)
{
    if (impl_->recording)
        impl_->boundBaseVertexIndex = index;
}

unsigned DX12PresentationDevice::BoundBaseVertexIndex() const
{
    return impl_->boundBaseVertexIndex;
}

bool DX12PresentationDevice::WaitForIdle()
{
    // Waiting for queued work does not submit an *open* frame.
    // Retention for that frame remains protected until EndFrame/Shutdown.
    if (!impl_->WaitForIdle())
        return false;
    impl_->CollectRetired();
    return true;
}

bool DX12PresentationDevice::Resize(unsigned width, unsigned height)
{
    Impl & state = *impl_;
    if (!IsReady() || state.recording)
        return state.Fail("Resize invalid state", E_INVALIDARG);
    // Minimized windows have zero client area. Preserve the existing buffers.
    if (width == 0 || height == 0 || (width == state.width && height == state.height))
        return true;
    if (!WaitForIdle())
        return false;
    state.passGraph.Reset(); // never retain pre-resize ID3D12Resource pointers

    for (auto & buffer : state.buffers)
        buffer.Reset();
    state.depthBuffer.Reset();
    if (!state.Check(state.swapChain->ResizeBuffers(BufferCount, width, height,
                      BackBufferFormat, 0), "IDXGISwapChain::ResizeBuffers") ||
        !state.CreateRenderTargets() || !state.CreateDepthTarget(width, height))
    {
        Shutdown();
        return false;
    }
    state.width = width;
    state.height = height;
    return true;
}

void DX12PresentationDevice::Shutdown()
{
    if (!impl_)
        return;
    Impl & state = *impl_;
    if (state.recording)
    {
        // Discard this unsubmitted frame; the last submitted fence still owns
        // every in-flight back buffer, so it is safe to wait before release.
        state.commandList->Close();
        state.recording = false;
    }
    state.passGraph.Reset();
    if (state.device && SUCCEEDED(state.device->GetDeviceRemovedReason()))
        state.WaitForIdle();
    // After GPU idle (or device removal), both retained generations and
    // unsubmitted staging allocations may be safely disposed.
    state.recordingResources.clear();
    state.retiredResources.clear();
    state.recordingObjects.clear();
    state.retiredObjects.clear();
    state.ReleaseAll();
}

void DX12PresentationDevice::SetSourceViewport(unsigned sourceWidth,
                                                unsigned sourceHeight)
{
    impl_->sourceAspect = sourceWidth != 0 && sourceHeight != 0
        ? static_cast<float>(sourceWidth) / static_cast<float>(sourceHeight)
        : 0.f;
}

float DX12PresentationDevice::SourceAspect() const { return impl_->sourceAspect; }
unsigned DX12PresentationDevice::Width() const { return impl_->width; }
unsigned DX12PresentationDevice::Height() const { return impl_->height; }
const char * DX12PresentationDevice::LastError() const { return impl_->lastError.c_str(); }
ID3D12Device * DX12PresentationDevice::NativeDevice() const { return impl_->device.Get(); }
ID3D12GraphicsCommandList * DX12PresentationDevice::ActiveCommandList() const
{
    return impl_->recording ? impl_->commandList.Get() : nullptr;
}
ID3D12Resource * DX12PresentationDevice::CurrentBackBuffer() const
{
    return impl_->recording ? impl_->buffers[impl_->currentBuffer].Get() : nullptr;
}
ID3D12Resource * DX12PresentationDevice::CurrentDepthTarget() const
{
    return impl_->recording ? impl_->depthBuffer.Get() : nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12PresentationDevice::CurrentBackBufferRTV() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE result = {};
    if (impl_->recording && impl_->rtvHeap)
    {
        result = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
        result.ptr += static_cast<SIZE_T>(impl_->currentBuffer) * impl_->rtvIncrement;
    }
    return result;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12PresentationDevice::CurrentDepthDSV() const
{
    return impl_->recording && impl_->dsvHeap
        ? impl_->dsvHeap->GetCPUDescriptorHandleForHeapStart()
        : D3D12_CPU_DESCRIPTOR_HANDLE{};
}
