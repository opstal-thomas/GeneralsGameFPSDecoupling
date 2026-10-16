/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
// M01: submit raw W3D R03 FVF geometry with native R02 buffer bindings,
// then read back actual WARP pixels through the game's R07 target.
#include "../DX12PresentationDevice.h"
#include "../DX12W3DNativeDraw.h"
#include "../DX12W3DGeometryBuffers.h"
#include "../DX12W3DRenderTargets.h"
#include "../DX12W3DTextureStages.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

using Microsoft::WRL::ComPtr;
using namespace DX12W3DGeometryBuffers;
namespace
{
    bool Check(bool ok, const char * label, const char * detail = "")
    {
        if (!ok)
            std::cerr << "M01 native indexed draw: " << label << " FAILED: " << detail << '\n';
        return ok;
    }
    struct Window
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND hwnd = nullptr;
        bool registered = false;
        bool Create()
        {
            WNDCLASSW wc = {};
            wc.hInstance = instance;
            wc.lpfnWndProc = DefWindowProcW;
            wc.lpszClassName = L"W3DDX12M01NativeDraw";
            registered = RegisterClassW(&wc) != 0;
            if (!registered) return false;
            hwnd = CreateWindowExW(0, wc.lpszClassName, L"M01 WARP raw FVF",
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                   220, 160, nullptr, nullptr, instance, nullptr);
            if (!hwnd) return false;
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            return true;
        }
        ~Window()
        {
            if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
            if (registered) UnregisterClassW(L"W3DDX12M01NativeDraw", instance);
        }
    };
    struct Vertex { float xyz[3]; std::uint32_t diffuse; };
    static_assert(sizeof(Vertex) == 16, "FVF XYZ|DIFFUSE");
    constexpr float Identity[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };

    bool ReadPixel(DX12PresentationDevice & device, unsigned char rgb[4],
                   ComPtr<ID3D12Resource> & readback,
                   UINT & rowPitch)
    {
        ID3D12Resource * const back = device.CurrentBackBuffer();
        const D3D12_RESOURCE_DESC texture = back->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT rows = 0;
        UINT64 bytesPerRow = 0, total = 0;
        device.NativeDevice()->GetCopyableFootprints(
            &texture, 0, 1, 0, &footprint, &rows, &bytesPerRow, &total);
        rowPitch = footprint.Footprint.RowPitch;
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC output = {};
        output.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        output.Width = total;
        output.Height = 1;
        output.DepthOrArraySize = 1;
        output.MipLevels = 1;
        output.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        output.SampleDesc.Count = 1;
        if (!Check(SUCCEEDED(device.NativeDevice()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &output, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&readback))), "create color readback"))
            return false;

        if (!Check(device.TransitionFrameResource(back, D3D12_RESOURCE_STATE_COPY_SOURCE),
                   "prepare primary for GPU readback", device.LastError()))
            return false;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = back;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        device.ActiveCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        return Check(device.TransitionFrameResource(back, D3D12_RESOURCE_STATE_RENDER_TARGET),
                     "restore primary color target", device.LastError());
    }

    bool CheckReadback(ID3D12Resource * readback, UINT pitch, unsigned expectedChannel)
    {
        void * pixels = nullptr;
        // Readback heaps are CPU-readable in their entirety. A nullptr range
        // avoids rejecting a renderer-dependent padded footprint size.
        const HRESULT mapResult = readback->Map(0, nullptr, &pixels);
        if (FAILED(mapResult) || !pixels)
        {
            std::cerr << "M01: readback Map HRESULT=0x" << std::hex
                      << static_cast<unsigned>(mapResult) << std::dec
                      << " resourceWidth=" << readback->GetDesc().Width
                      << " pitch=" << pitch << '\n';
            return Check(false, "Map actual native WARP render target");
        }
        const auto * pixel = static_cast<const unsigned char *>(pixels) + 60 * pitch + 80 * 4;
        const bool correct = pixel[expectedChannel] > 180 &&
                             pixel[(expectedChannel + 1) % 3] < 80 &&
                             pixel[(expectedChannel + 2) % 3] < 80;
        if (!correct)
            std::cerr << "M01 readback pixel RGBA=("
                      << unsigned(pixel[0]) << "," << unsigned(pixel[1])
                      << "," << unsigned(pixel[2]) << ","
                      << unsigned(pixel[3]) << ") pitch=" << pitch
                      << " expected dominant channel=" << expectedChannel << '\n';
        const D3D12_RANGE unused = {0, 0};
        readback->Unmap(0, &unused);
        return Check(correct, "authoritative W3D vertex color reaches WARP pixel");
    }
}

int wmain()
{
    Window window;
    if (!Check(window.Create(), "create WARP HWND")) return 1;
    DX12PresentationDevice device;
    if (!Check(device.Initialize(window.hwnd, 160, 120, true),
               "initialize DX12 WARP", device.LastError())) return 1;
    DX12W3DRenderTargets targets;
    DX12W3DTextureStages textureStages;
    DX12W3DNativeDraw draw;
    RenderBufferDescription vbDesc;
    vbDesc.byteSize = sizeof(Vertex) * 4;
    vbDesc.legacyVertexFormat = 0x0042; // XYZ|DIFFUSE, original D3DCOLOR
    RenderBufferDescription r16;
    r16.byteSize = sizeof(std::uint16_t) * 4;
    RenderBufferDescription r32;
    r32.byteSize = sizeof(std::uint32_t) * 4;
    r32.indexFormat = RENDER_INDEX_UINT32;
    auto * red = CreateVertex(device.NativeDevice(), vbDesc);
    auto * green = CreateVertex(device.NativeDevice(), vbDesc);
    auto * indices16 = CreateIndex(device.NativeDevice(), r16);
    auto * indices32 = CreateIndex(device.NativeDevice(), r32);
    if (!Check(red && green && indices16 && indices32,
               "allocate actual W3D FVF vertex and R16/R32 index resources")) return 1;

    const std::array<Vertex, 4> vRed = {{
        {{-0.8f, -0.8f, 0.5f}, 0xffff0000},
        {{0.f, 0.8f, 0.5f}, 0xffff0000},
        {{0.8f, -0.8f, 0.5f}, 0xffff0000},
        {{0.8f, 0.8f, 0.5f}, 0xffff0000}
    }};
    const std::array<Vertex, 4> vGreen = {{
        {{-0.8f, -0.8f, 0.5f}, 0xff00ff00},
        {{-0.8f, 0.8f, 0.5f}, 0xff00ff00},
        {{0.8f, -0.8f, 0.5f}, 0xff00ff00},
        {{0.8f, 0.8f, 0.5f}, 0xff00ff00}
    }};
    const std::array<std::uint16_t, 4> i16 = {0,1,2,3};
    const std::array<std::uint32_t, 4> i32 = {0,1,2,3};
    void * a = MapVertex(red, 0, sizeof(vRed), RENDER_MAP_NONE);
    void * b = MapVertex(green, 0, sizeof(vGreen), RENDER_MAP_NONE);
    void * c = MapIndex(indices16, 0, sizeof(i16), RENDER_MAP_NONE);
    void * d = MapIndex(indices32, 0, sizeof(i32), RENDER_MAP_NONE);
    if (!Check(a && b && c && d, "Map four W3D resource shadows")) return 1;
    std::memcpy(a, vRed.data(), sizeof(vRed));
    std::memcpy(b, vGreen.data(), sizeof(vGreen));
    std::memcpy(c, i16.data(), sizeof(i16));
    std::memcpy(d, i32.data(), sizeof(i32));
    if (!Check(UnmapVertex(red) && UnmapVertex(green) &&
               UnmapIndex(indices16) && UnmapIndex(indices32), "commit W3D geometry"))
        return 1;

    const float clear[] = {0.05f, 0.06f, 0.08f, 1.f};
    for (unsigned pass = 0; pass != 2; ++pass)
    {
        if (!Check(device.BeginFrame(clear), "begin authoritative frame") ||
            !Check(targets.BeginFrame(device), "attach R07 native render targets") ||
            !Check(BindVertex(device, pass ? green : red, 0, sizeof(Vertex)),
                   "bind original packed-color FVF vertex stream", LastError()) ||
            !Check(BindIndex(device, pass ? indices32 : indices16, 0),
                   "bind original R16/R32 index buffer", LastError()))
            return 1;
        draw.BeginFrame();
        draw.BindVertex(NativeVertexLayout(pass ? green : red), 4);
        draw.BindIndex(4);
        // Verify out-of-bounds draws are rejected rather than submitting bad
        // D3D12 index ranges that could remove a device.
        if (!Check(!draw.Draw(device, targets, textureStages, Identity,
                              false, 3, 2, 0, 4),
                   "reject out-of-range game draw"))
            return 1;
        if (!Check(draw.Draw(device, targets, textureStages, Identity,
                             pass != 0, 0, pass != 0 ? 2 : 1, 0, 4),
                   "draw raw FVF game geometry, no preview mesh", draw.LastError()) ||
            !Check(draw.DrawCount() == 1 && draw.PipelineCount() >= 1,
                   "native W3D PSO cache submitted exactly one draw"))
            return 1;
        ComPtr<ID3D12Resource> readback;
        UINT rowPitch = 0;
        unsigned char unused[4] = {};
        if (!ReadPixel(device, unused, readback, rowPitch) ||
            !Check(targets.EndFrame(device), "restore R07 primary") ||
            !Check(device.EndFrame(0, false) && device.WaitForIdle(),
                   "submit actual GPU draw and wait for fence", device.LastError()) ||
            !CheckReadback(readback.Get(), rowPitch, pass == 0 ? 0 : 1))
            return 1;
    }

    ReleaseVertex(red);
    ReleaseVertex(green);
    ReleaseIndex(indices16);
    ReleaseIndex(indices32);
    device.Shutdown();
    std::cout << "M01 PASS: original FVF R16 triangles and R32 strips draw natively with WARP readback\n";
    return 0;
}
