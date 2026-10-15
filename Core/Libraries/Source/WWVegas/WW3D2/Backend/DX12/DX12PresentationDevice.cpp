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
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr UINT BufferCount = 2;
    constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT DepthBufferFormat = DXGI_FORMAT_D32_FLOAT;
    constexpr DWORD FenceTimeoutMs = 30000;

    D3D12_RESOURCE_BARRIER Transition(ID3D12Resource * resource,
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
    UINT rtvIncrement = 0;
    UINT currentBuffer = 0;
    unsigned width = 0;
    unsigned height = 0;
    float sourceAspect = 0.f;
    bool recording = false;
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

bool DX12PresentationDevice::BeginFrame(const float clearColor[4])
{
    Impl & state = *impl_;
    if (!IsReady() || state.recording || !clearColor)
        return state.Fail("BeginFrame invalid state", E_INVALIDARG);

    state.currentBuffer = state.swapChain->GetCurrentBackBufferIndex();
    if (state.currentBuffer >= BufferCount)
        return state.Fail("Invalid swap-chain buffer index", E_FAIL);

    const UINT index = state.currentBuffer;
    if (!state.WaitForFence(state.frameFences[index]))
        return false;
    if (!state.Check(state.allocators[index]->Reset(), "Reset frame command allocator"))
        return false;
    if (!state.Check(state.commandList->Reset(state.allocators[index].Get(), nullptr),
                     "Reset frame command list"))
        return false;

    const auto barrier = Transition(state.buffers[index].Get(),
                                    D3D12_RESOURCE_STATE_PRESENT,
                                    D3D12_RESOURCE_STATE_RENDER_TARGET);
    state.commandList->ResourceBarrier(1, &barrier);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = state.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(index) * state.rtvIncrement;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        state.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    state.commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    state.commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    state.commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH,
                                             1.f, 0, 0, nullptr);
    state.recording = true;
    return true;
}

bool DX12PresentationDevice::EndFrame(unsigned syncInterval)
{
    Impl & state = *impl_;
    if (!state.recording || syncInterval > 4)
        return state.Fail("EndFrame invalid state or sync interval", E_INVALIDARG);

    const auto barrier = Transition(state.buffers[state.currentBuffer].Get(),
                                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                                    D3D12_RESOURCE_STATE_PRESENT);
    state.commandList->ResourceBarrier(1, &barrier);
    state.recording = false;
    if (!state.Check(state.commandList->Close(), "Close frame command list"))
        return false;

    ID3D12CommandList * lists[] = {state.commandList.Get()};
    state.queue->ExecuteCommandLists(1, lists);
    const HRESULT presented = state.swapChain->Present(syncInterval, 0);
    const UINT64 signalValue = state.nextFence++;
    const HRESULT signalled = state.queue->Signal(state.fence.Get(), signalValue);
    if (!state.Check(signalled, "Signal frame fence"))
        return false;
    state.frameFences[state.currentBuffer] = signalValue;
    return state.Check(presented, "IDXGISwapChain::Present");
}

bool DX12PresentationDevice::WaitForIdle()
{
    return impl_->WaitForIdle();
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
    if (state.device && SUCCEEDED(state.device->GetDeviceRemovedReason()))
        state.WaitForIdle();
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
