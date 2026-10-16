/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
// R05: authoritative DX12 SRV tables, not the DX8 preview's private heap.
// Draw and read back sampled pixels from different immutable material tables
// recorded on the same command list, including update, unbind and final release.
#include "../DX12PresentationDevice.h"
#include "../DX12W3DTextures.h"
#include "../DX12W3DTextureStages.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
    struct Window
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND hwnd = nullptr;
        bool registered = false;
        bool Create()
        {
            WNDCLASSW cls = {};
            cls.hInstance = instance;
            cls.lpfnWndProc = DefWindowProcW;
            cls.lpszClassName = L"W3DDX12R05WARP";
            registered = RegisterClassW(&cls) != 0;
            if (!registered) return false;
            hwnd = CreateWindowExW(0, cls.lpszClassName, L"R05 SRV descriptor smoke",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  320, 240, nullptr, nullptr, instance, nullptr);
            return hwnd != nullptr;
        }
        ~Window()
        {
            if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
            if (registered) UnregisterClassW(L"W3DDX12R05WARP", instance);
        }
    };

    bool Check(bool value, const char * label)
    {
        if (!value)
            std::cerr << "DX12-R05 FAIL " << label << ": "
                      << DX12W3DTextures::LastError() << "\n";
        return value;
    }

    RenderTextureHandle T(RenderTexture2DHandle t)
    {
        return reinterpret_cast<RenderTextureHandle>(t);
    }
    RenderTextureHandle T(RenderTextureCubeHandle t)
    {
        return reinterpret_cast<RenderTextureHandle>(t);
    }
    RenderTextureHandle T(RenderTextureVolumeHandle t)
    {
        return reinterpret_cast<RenderTextureHandle>(t);
    }

    RenderTexture2DHandle MakeTexture(ID3D12Device * device,
                                      const std::array<std::uint8_t, 4> & bgra)
    {
        RenderTexture2DDescription desc;
        desc.width = desc.height = 4;
        desc.pixelFormat = 2; // WW3D A8R8G8B8; source bytes in BGRA order
        desc.mipLevels = 3;
        auto tex = DX12W3DTextures::Create2D(device, desc);
        if (!tex) return nullptr;
        RenderSurfaceMap mapped;
        if (!DX12W3DTextures::Map2D(tex, 0, mapped))
        {
            DX12W3DTextures::Release(T(tex));
            return nullptr;
        }
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 4; ++x)
                std::memcpy(static_cast<std::uint8_t *>(mapped.data) +
                            y * mapped.rowPitch + x * 4, bgra.data(), 4);
        DX12W3DTextures::Unmap2D(tex, 0);
        return tex;
    }

    const char ShaderCode[] = R"(
        Texture2D<float4> t0 : register(t0);
        Texture2D<float4> t1 : register(t1);
        cbuffer Params : register(b0) { uint selectedStage; };
        struct Vertex { float4 position : SV_POSITION; };
        Vertex VSMain(uint id : SV_VertexID)
        {
            Vertex v;
            float2 p = id == 0 ? float2(-1, -1) :
                       id == 1 ? float2(-1, 3) : float2(3, -1);
            v.position = float4(p, 0.5, 1);
            return v;
        }
        float4 PSMain(Vertex v) : SV_TARGET
        {
            if (selectedStage == 0) return t0.Load(int3(0, 0, 0));
            return t1.Load(int3(0, 0, 0));
        }
    )";

    bool BuildPipeline(ID3D12Device * device, ComPtr<ID3D12RootSignature> & root,
                       ComPtr<ID3D12PipelineState> & pipeline)
    {
        // Pipeline states/root signatures belong to a specific ID3D12Device.
        // Do not pass a non-null ComPtr output pointer after a device reset.
        pipeline.Reset();
        root.Reset();
        ComPtr<ID3DBlob> vs, ps, errors;
        if (!Check(SUCCEEDED(D3DCompile(
                      ShaderCode, sizeof(ShaderCode)-1, nullptr, nullptr,
                      nullptr, "VSMain", "vs_5_1", 0, 0, &vs, &errors)),
                   "Compile R05 vertex shader"))
            return false;
        errors.Reset();
        if (!Check(SUCCEEDED(D3DCompile(
                      ShaderCode, sizeof(ShaderCode)-1, nullptr, nullptr,
                      nullptr, "PSMain", "ps_5_1", 0, 0, &ps, &errors)),
                   "Compile R05 SRV pixel shader"))
        {
            if (errors)
                std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
            return false;
        }

        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        D3D12_ROOT_PARAMETER params[3] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = 1;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        for (unsigned i = 0; i < 2; ++i)
        {
            ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[i].NumDescriptors = 1;
            ranges[i].BaseShaderRegister = i;
            params[i+1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i+1].DescriptorTable.NumDescriptorRanges = 1;
            params[i+1].DescriptorTable.pDescriptorRanges = &ranges[i];
            params[i+1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_SIGNATURE_DESC signature = {};
        signature.NumParameters = 3;
        signature.pParameters = params;
        signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> encoded;
        if (!Check(SUCCEEDED(D3D12SerializeRootSignature(
                       &signature, D3D_ROOT_SIGNATURE_VERSION_1,
                       &encoded, &errors)), "Serialize descriptor root signature") ||
            !Check(SUCCEEDED(device->CreateRootSignature(
                       0, encoded->GetBufferPointer(), encoded->GetBufferSize(),
                       IID_PPV_ARGS(&root))), "Create descriptor root signature"))
            return false;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC p = {};
        p.pRootSignature = root.Get();
        p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        p.RasterizerState.DepthClipEnable = TRUE;
        p.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        p.SampleMask = UINT_MAX;
        p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.NumRenderTargets = 1;
        p.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        p.SampleDesc.Count = 1;
        return Check(SUCCEEDED(device->CreateGraphicsPipelineState(
                         &p, IID_PPV_ARGS(&pipeline))),
                     "Build R05 two-stage shader pipeline");
    }

    struct Readback
    {
        ComPtr<ID3D12Resource> buffer;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
        UINT64 size = 0;
    };

    bool CopyBackbuffer(DX12PresentationDevice & device, Readback & readback)
    {
        ID3D12Resource * back = device.CurrentBackBuffer();
        if (!back) return false;
        const auto description = back->GetDesc();
        device.NativeDevice()->GetCopyableFootprints(
            &description, 0, 1, 0, &readback.layout,
            nullptr, nullptr, &readback.size);
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC buffer = {};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = readback.size;
        buffer.Height = 1;
        buffer.DepthOrArraySize = buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device.NativeDevice()->CreateCommittedResource(
                     &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                     IID_PPV_ARGS(&readback.buffer))) ||
            !device.TransitionFrameResource(back, D3D12_RESOURCE_STATE_COPY_SOURCE))
            return false;
        D3D12_TEXTURE_COPY_LOCATION to = {}, from = {};
        to.pResource = readback.buffer.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint = readback.layout;
        from.pResource = back;
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.SubresourceIndex = 0;
        device.ActiveCommandList()->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        return true;
    }

    bool CheckPixel(const Readback & r, unsigned x, unsigned y,
                    const std::array<std::uint8_t, 3> & rgb)
    {
        const D3D12_RANGE read = {0, static_cast<SIZE_T>(r.size)};
        std::uint8_t * map = nullptr;
        if (!Check(SUCCEEDED(r.buffer->Map(0, &read,
                            reinterpret_cast<void **>(&map))), "Map GPU readback"))
            return false;
        const auto * pixel = map + r.layout.Offset +
                             static_cast<std::size_t>(y) * r.layout.Footprint.RowPitch +
                             x * 4;
        const bool okay = pixel[0] == rgb[0] &&
                          pixel[1] == rgb[1] && pixel[2] == rgb[2];
        if (!okay)
            std::cerr << "Pixel (" << x << ", " << y << ") expected "
                      << int(rgb[0]) << "," << int(rgb[1]) << "," << int(rgb[2])
                      << " got " << int(pixel[0]) << "," << int(pixel[1]) << ","
                      << int(pixel[2]) << "\n";
        const D3D12_RANGE zero = {0, 0};
        r.buffer->Unmap(0, &zero);
        return Check(okay, "Verify sampled SRV pixel readback");
    }

    bool DrawArea(DX12PresentationDevice & device, DX12W3DTextureStages & stages,
                  unsigned stage, LONG x, LONG y)
    {
        if (!stages.BindForDraw(device, 1))
        {
            std::cerr << stages.LastError() << "\n";
            return false;
        }
        auto * cmd = device.ActiveCommandList();
        const D3D12_RECT scissor = {x, y, x + 32, y + 32};
        cmd->RSSetScissorRects(1, &scissor);
        cmd->SetGraphicsRoot32BitConstant(0, stage, 0);
        cmd->DrawInstanced(3, 1, 0, 0);
        return true;
    }
}

int wmain()
{
    using namespace DX12W3DTextures;
    Window window;
    if (!Check(window.Create(), "Create native WARP test window")) return 1;
    DX12PresentationDevice device;
    if (!Check(device.Initialize(window.hwnd, 64, 64, true), "Initialize WARP D3D12"))
        return 1;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    if (!BuildPipeline(device.NativeDevice(), root, pipeline)) return 1;

    DX12W3DTextureStages stages;
    auto red = MakeTexture(device.NativeDevice(), {0, 0, 255, 255});
    auto blue = MakeTexture(device.NativeDevice(), {255, 0, 0, 255});
    if (!Check(red && blue, "Create real W3D ARGB8 source textures") ||
        !Check(stages.Bind(0, T(red)) && stages.Bind(1, T(blue)),
               "Retain stage 0/1 native texture identities"))
        return 1;
    // Drop all caller references. The stage cache must be the last valid
    // CPU owner; later unbind must not touch stale or recycled handles.
    Release(T(red));
    Release(T(blue));

    if (!Check(!stages.Bind(8, nullptr), "Reject out-of-range stage") ||
        !Check(stages.Bound(0) != nullptr && stages.Bound(1) != nullptr,
               "Bad stage bind leaves existing stage references intact"))
        return 1;

    RenderTextureCubeDescription cubeDesc;
    cubeDesc.edgeLength = 4; cubeDesc.pixelFormat = 2; cubeDesc.mipLevels = 2;
    auto cube = CreateCube(device.NativeDevice(), cubeDesc);
    RenderTextureVolumeDescription volumeDesc;
    volumeDesc.width = volumeDesc.height = 4; volumeDesc.depth = 2;
    volumeDesc.pixelFormat = 2; volumeDesc.mipLevels = 2;
    auto volume = CreateVolume(device.NativeDevice(), volumeDesc);
    if (!Check(cube && volume &&
               stages.Bind(2, T(cube)) && stages.Bind(3, T(volume)),
               "Retain cube/volume handles on additional W3D stages"))
        return 1;
    Release(T(cube));
    Release(T(volume));

    const float clear[] = {0.30f, 0.30f, 0.30f, 1.f};
    if (!Check(device.BeginFrame(clear), "Begin native shader-visible SRV frame") ||
        !Check(stages.PrepareBound(device), "Upload bound textures once per W3D revision"))
        return 1;
    ViewSnapshot cubeView, volumeView;
    if (!Check(Snapshot(stages.Bound(2), cubeView) &&
               cubeView.descriptor.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE &&
               cubeView.descriptor.TextureCube.MipLevels == 2,
               "SRV descriptors expose cubemap faces/mips") ||
        !Check(Snapshot(stages.Bound(3), volumeView) &&
               volumeView.descriptor.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE3D &&
               volumeView.descriptor.Texture3D.MipLevels == 2,
               "SRV descriptors expose 3D volume mip chains"))
        return 1;
    auto * cmd = device.ActiveCommandList();
    cmd->SetGraphicsRootSignature(root.Get());
    cmd->SetPipelineState(pipeline.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {0, 0, 64, 64, 0, 1};
    cmd->RSSetViewports(1, &viewport);

    if (!Check(DrawArea(device, stages, 0, 0, 0), "Draw left-top from original SRV") ||
        !Check(DrawArea(device, stages, 1, 32, 0), "Draw right-top from second SRV") ||
        !Check(stages.SnapshotCount() == 1 && stages.PageCount() == 1,
               "Cache unchanged t0/t1 pair without descriptor rewrites"))
        return 1;

    const auto previous = NativeResource(stages.Bound(0));
    ViewSnapshot before;
    Snapshot(stages.Bound(0), before);
    RenderSurfaceMap pixels;
    if (!Check(Map2D(reinterpret_cast<RenderTexture2DHandle>(stages.Bound(0)),
                     0, pixels), "Map live stage0 texture for a same-frame update"))
        return 1;
    const std::array<std::uint8_t, 4> green = {0, 255, 0, 255};
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            std::memcpy(static_cast<std::uint8_t *>(pixels.data) +
                        y * pixels.rowPitch + x * 4, green.data(), 4);
    if (!Check(Unmap2D(
                   reinterpret_cast<RenderTexture2DHandle>(stages.Bound(0)), 0),
               "Unmap edited stage0 revision") ||
        !Check(DrawArea(device, stages, 0, 0, 32),
               "Draw left-bottom with new GPU texture generation"))
        return 1;
    ViewSnapshot after;
    if (!Check(Snapshot(stages.Bound(0), after) &&
               after.gpuGeneration != before.gpuGeneration &&
               after.contentRevision != before.contentRevision &&
               NativeResource(stages.Bound(0)) != previous,
               "Content update invalidated immutable SRV and GPU generation"))
        return 1;

    if (!Check(stages.Bind(0, nullptr) && stages.Bind(1, nullptr),
               "Null unbinding drops old stage references") ||
        !Check(DrawArea(device, stages, 0, 32, 32),
               "Draw null SRV instead of stale freed texture") ||
        !Check(stages.SnapshotCount() == 3,
               "Null and updated SRVs produce separate immutable tables") ||
        !Check(device.InFlightObjectCount() >= 1,
               "Shader-visible heap retained through the native GPU frame fence"))
        return 1;
    Readback readback;
    if (!Check(CopyBackbuffer(device, readback),
               "GPU copy backbuffer with four different SRV bindings"))
        return 1;
    // All descriptors and textures may lose their last CPU owner BEFORE
    // ExecuteCommandLists. Only the frame fence is allowed to free them.
    stages.Clear();
    if (!Check(device.InFlightObjectCount() >= 1,
               "Discarding stage cache does not free in-flight SRV heaps") ||
        !Check(device.EndFrame(0, false), "Submit cached descriptor material draws") ||
        !Check(device.WaitForIdle(), "Wait for WARP shader sampling") ||
        !Check(CheckPixel(readback, 16, 16, {255, 0, 0}),
               "Old red SRV survived same-frame texture mutation") ||
        !Check(CheckPixel(readback, 48, 16, {0, 0, 255}),
               "Stage1 native GPU SRV sampled blue") ||
        !Check(CheckPixel(readback, 16, 48, {0, 255, 0}),
               "Updated stage0 shader resource sampled green") ||
        !Check(CheckPixel(readback, 48, 48, {0, 0, 0}),
               "Null stage SRV returned black after texture release") ||
        !Check(device.InFlightObjectCount() == 0,
               "All descriptor heaps retired after GPU fence completion"))
        return 1;

    // Reusing the same cache after device recreation may NEVER bind a view
    // into the previous device or reissue a GPU descriptor from old frames.
    auto again = MakeTexture(device.NativeDevice(), {4, 8, 12, 255});
    if (!Check(again && stages.Bind(0, T(again)),
               "Keep native identity through device teardown")) return 1;
    Release(T(again));
    device.Shutdown();
    if (!Check(device.Initialize(window.hwnd, 64, 64, true), "Recreate WARP device") ||
        !Check(device.BeginFrame(clear), "Begin first frame after device recovery") ||
        !Check(stages.PrepareBound(device) && stages.Bound(0),
               "Rehydrate texture GPU resource on the recreated device"))
        return 1;
    ViewSnapshot reset;
    if (!Check(Snapshot(stages.Bound(0), reset) && reset.resource &&
               reset.gpuGeneration > 0 && reset.descriptor.Texture2D.MipLevels == 3,
               "New device SRV preserves W3D texture mips") ||
        !Check(stages.PageCount() == 0,
               "Stale SRV pages are abandoned on device-reset frame"))
        return 1;
    // A native draw pass must set its root signature first. Rebuild it for the
    // newly created ID3D12Device, then issue the next immutable table.
    if (!BuildPipeline(device.NativeDevice(), root, pipeline)) return 1;
    device.ActiveCommandList()->SetGraphicsRootSignature(root.Get());
    if (!Check(stages.BindForDraw(device, 1) && stages.SnapshotCount() == 1,
               "Fresh device/frame gets new shader-visible descriptors") ||
        !Check(device.EndFrame(0, false), "Submit reset-recovery SRV table") ||
        !Check(device.WaitForIdle(), "Complete recovered texture bind"))
        return 1;
    stages.Clear();
    device.Shutdown();
    std::cout << "R05 PASS: immutable two-stage SRV tables, WARP sampled pixels, "
                 "update/unbind/release, cube/volume view shape, descriptor "
                 "fence ownership and new-device invalidation\n";
    return 0;
}
