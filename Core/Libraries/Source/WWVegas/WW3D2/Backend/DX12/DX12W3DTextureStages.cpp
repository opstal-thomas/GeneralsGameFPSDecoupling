/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#include "DX12W3DTextureStages.h"
#include "DX12W3DTextures.h"
#include "DX12PresentationDevice.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    // A modest shader-visible descriptor page supports 256 distinct material
    // pairs. A new page is allocated only if the W3D draw stream exhausts it.
    // Pages are immutable once published and retained by the GPU fence.
    constexpr unsigned DescriptorsPerPage = 512;
    constexpr unsigned DescriptorsPerDraw = 2;

    struct TableKey
    {
        std::array<std::uint64_t, 6> words = {};
        bool operator==(const TableKey & other) const
        {
            return words == other.words;
        }
    };
    struct TableHasher
    {
        std::size_t operator()(const TableKey & key) const
        {
            std::size_t h = 0x9E3779B9u;
            for (std::uint64_t value : key.words)
                h ^= static_cast<std::size_t>(value ^ (value >> 32)) +
                     static_cast<std::size_t>(0x9E3779B9u) + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct Table
    {
        std::size_t page = 0;
        unsigned offset = 0;
    };
    struct HeapPage
    {
        ComPtr<ID3D12DescriptorHeap> heap;
        unsigned used = 0;
    };
    D3D12_SHADER_RESOURCE_VIEW_DESC NullView()
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Texture2D.MipLevels = 1;
        return d;
    }
}

struct DX12W3DTextureStages::Impl
{
    std::array<RenderTextureHandle, DX12W3DTextureStages::StageCount> stages = {};
    const DX12PresentationDevice * recordingOwner = nullptr;
    ID3D12Device * recordingDevice = nullptr;
    unsigned long long recordingSerial = 0;
    unsigned descriptorStride = 0;
    unsigned snapshotCount = 0;
    std::vector<HeapPage> pages;
    std::unordered_map<TableKey, Table, TableHasher> tables;
    // A content revision prepared on this recording is already pinned by
    // RetainFrameResource. Do not repeat AddRef for every draw using it.
    std::unordered_map<std::uint64_t, std::uint64_t> preparedRevisions;
    std::string error;

    bool Fail(const std::string & message)
    {
        error = message;
        return false;
    }

    void ClearFrame()
    {
        // Old pages are *also* referenced by RetainFrameObject. Clearing the
        // CPU cache never invalidates earlier recorded GPU descriptor tables.
        recordingOwner = nullptr;
        recordingDevice = nullptr;
        recordingSerial = 0;
        descriptorStride = 0;
        snapshotCount = 0;
        pages.clear();
        tables.clear();
        preparedRevisions.clear();
    }

    bool BeginFrame(DX12PresentationDevice & frame)
    {
        if (!frame.IsRecording() || !frame.NativeDevice() ||
            !frame.ActiveCommandList() || !frame.RecordingSerial())
            return Fail("Texture stages require an open native D3D12 frame");
        if (recordingOwner == &frame && recordingDevice == frame.NativeDevice() &&
            recordingSerial == frame.RecordingSerial())
            return true;
        ClearFrame();
        recordingOwner = &frame;
        recordingDevice = frame.NativeDevice();
        recordingSerial = frame.RecordingSerial();
        descriptorStride = recordingDevice->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return descriptorStride ? true : Fail("Cannot determine DX12 SRV descriptor stride");
    }

    bool Prepare(DX12PresentationDevice & frame, RenderTextureHandle h,
                 DX12W3DTextures::ViewSnapshot & output)
    {
        output = DX12W3DTextures::ViewSnapshot();
        if (!h)
            return true; // an explicit null descriptor, never a stale SRV
        if (!DX12W3DTextures::Snapshot(h, output))
            return Fail(DX12W3DTextures::LastError());
        const auto found = preparedRevisions.find(output.identity);
        // Once per CPU mip generation per frame. A same-frame update changes
        // contentRevision, reuploads to a distinct native resource and gives
        // the next draw a new descriptor (old draws remain unchanged).
        if (!output.dirty && output.resource &&
            found != preparedRevisions.end() &&
            found->second == output.contentRevision)
        {
            // Readback/R07 passes may have transitioned the texture away
            // from shader read since its last draw. Avoid duplicate AddRef
            // but still respect the actual B05 resource-state tracker.
            D3D12_RESOURCE_STATES current = D3D12_RESOURCE_STATE_COMMON;
            const auto sampled = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            if (frame.GetFrameResourceState(output.resource.Get(), current))
                return current == sampled ||
                    frame.TransitionFrameResource(output.resource.Get(), sampled) ||
                    Fail(frame.LastError());
        }
        if (!DX12W3DTextures::Prepare(frame, h))
            return Fail(DX12W3DTextures::LastError());
        if (!DX12W3DTextures::Snapshot(h, output))
            return Fail(DX12W3DTextures::LastError());
        if (!output.resource || output.dirty || !output.gpuGeneration)
            return Fail("Native W3D texture was not resident after GPU upload");
        preparedRevisions[output.identity] = output.contentRevision;
        return true;
    }

    bool AddPage(DX12PresentationDevice & frame)
    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        d.NumDescriptors = DescriptorsPerPage;
        d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        HeapPage page;
        if (FAILED(recordingDevice->CreateDescriptorHeap(
                &d, IID_PPV_ARGS(&page.heap))))
            return Fail("Creating DX12 W3D shader-visible SRV heap failed");
        if (!frame.RetainFrameObject(page.heap.Get()))
            return Fail(frame.LastError());
        pages.push_back(std::move(page));
        return true;
    }
};

DX12W3DTextureStages::DX12W3DTextureStages() : impl_(new Impl) {}
DX12W3DTextureStages::~DX12W3DTextureStages() { Clear(); }

bool DX12W3DTextureStages::Bind(unsigned stage, RenderTextureHandle texture)
{
    Impl & state = *impl_;
    state.error.clear();
    if (stage >= StageCount)
        return state.Fail("W3D texture stage index exceeds native stage capacity");
    auto & slot = state.stages[stage];
    if (slot != texture)
    {
        DX12W3DTextures::Retain(texture);
        DX12W3DTextures::Release(slot);
        slot = texture;
    }
    return true;
}

RenderTextureHandle DX12W3DTextureStages::Bound(unsigned stage) const
{
    return stage < StageCount ? impl_->stages[stage] : nullptr;
}

void DX12W3DTextureStages::Clear()
{
    Impl & state = *impl_;
    for (auto & texture : state.stages)
    {
        DX12W3DTextures::Release(texture);
        texture = nullptr;
    }
    state.ClearFrame();
    state.error.clear();
}

bool DX12W3DTextureStages::PrepareBound(DX12PresentationDevice & frame)
{
    Impl & state = *impl_;
    state.error.clear();
    if (!state.BeginFrame(frame))
        return false;
    for (const auto texture : state.stages)
    {
        if (texture)
        {
            DX12W3DTextures::ViewSnapshot view;
            if (!state.Prepare(frame, texture, view))
                return false;
        }
    }
    return true;
}

bool DX12W3DTextureStages::BindForDraw(DX12PresentationDevice & frame,
                                        unsigned firstRoot)
{
    Impl & state = *impl_;
    state.error.clear();
    if (!state.BeginFrame(frame) || firstRoot > 62)
        return state.Fail("SRV binding requires valid root descriptor-table slots");
    std::array<DX12W3DTextures::ViewSnapshot, DescriptorsPerDraw> views;
    TableKey key;
    for (unsigned stage = 0; stage < DescriptorsPerDraw; ++stage)
    {
        if (!state.Prepare(frame, state.stages[stage], views[stage]))
            return false;
        key.words[stage * 3] = views[stage].identity;
        key.words[stage * 3 + 1] = views[stage].contentRevision;
        key.words[stage * 3 + 2] = views[stage].gpuGeneration;
    }

    Table table;
    const auto cached = state.tables.find(key);
    if (cached != state.tables.end())
        table = cached->second;
    else
    {
        if (state.pages.empty() ||
            state.pages.back().used + DescriptorsPerDraw > DescriptorsPerPage)
        {
            if (!state.AddPage(frame))
                return false;
        }
        HeapPage & page = state.pages.back();
        table.page = state.pages.size() - 1;
        table.offset = page.used;
        const auto base = page.heap->GetCPUDescriptorHandleForHeapStart();
        for (unsigned stage = 0; stage < DescriptorsPerDraw; ++stage)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE slot = base;
            slot.ptr += static_cast<SIZE_T>(page.used + stage) * state.descriptorStride;
            const auto desc = views[stage].resource
                ? views[stage].descriptor : NullView();
            // A null resource gets a real D3D12 null descriptor (black/zero
            // when sampled), rather than reusing the previous stage's SRV.
            state.recordingDevice->CreateShaderResourceView(
                views[stage].resource.Get(), &desc, slot);
        }
        page.used += DescriptorsPerDraw;
        state.tables.emplace(key, table);
        ++state.snapshotCount;
    }

    ID3D12DescriptorHeap * const heap = state.pages[table.page].heap.Get();
    auto * const cmd = frame.ActiveCommandList();
    cmd->SetDescriptorHeaps(1, &heap);
    D3D12_GPU_DESCRIPTOR_HANDLE descriptor = heap->GetGPUDescriptorHandleForHeapStart();
    descriptor.ptr += static_cast<UINT64>(table.offset) * state.descriptorStride;
    for (unsigned stage = 0; stage < DescriptorsPerDraw; ++stage)
    {
        cmd->SetGraphicsRootDescriptorTable(firstRoot + stage, descriptor);
        descriptor.ptr += state.descriptorStride;
    }
    return true;
}

unsigned DX12W3DTextureStages::SnapshotCount() const
{
    return impl_->snapshotCount;
}

unsigned DX12W3DTextureStages::PageCount() const
{
    return static_cast<unsigned>(impl_->pages.size());
}

const char * DX12W3DTextureStages::LastError() const
{
    return impl_->error.c_str();
}
