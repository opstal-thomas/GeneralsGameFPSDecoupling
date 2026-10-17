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
#include "../DX12W3DTextures.h"
#include "../DX12W3DSurfaces.h"
#include "../DX12W3DSurfaceTransfers.h"
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
    struct LitVertex { float xyz[3]; float normal[3]; std::uint32_t diffuse; };
    static_assert(sizeof(LitVertex) == 28, "FVF XYZ|NORMAL|DIFFUSE");
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
        const bool correct = expectedChannel == 3 ?
            pixel[0] < 8 && pixel[1] < 8 && pixel[2] < 8 :
            (pixel[expectedChannel] > 180 &&
             pixel[(expectedChannel + 1) % 3] < 80 &&
             pixel[(expectedChannel + 2) % 3] < 80);
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
    RenderBufferDescription litDesc;
    litDesc.byteSize = sizeof(LitVertex) * 4;
    litDesc.legacyVertexFormat = 0x0052; // XYZ | NORMAL | DIFFUSE
    RenderBufferDescription r16;
    r16.byteSize = sizeof(std::uint16_t) * 4;
    RenderBufferDescription r32;
    r32.byteSize = sizeof(std::uint32_t) * 4;
    r32.indexFormat = RENDER_INDEX_UINT32;
    auto * red = CreateVertex(device.NativeDevice(), vbDesc);
    auto * green = CreateVertex(device.NativeDevice(), vbDesc);
    auto * lit = CreateVertex(device.NativeDevice(), litDesc);
    auto * indices16 = CreateIndex(device.NativeDevice(), r16);
    auto * indices32 = CreateIndex(device.NativeDevice(), r32);
    if (!Check(red && green && lit && indices16 && indices32,
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
    const std::array<LitVertex, 4> vLit = {{
        {{-0.8f, -0.8f, 0.5f}, {0.f, 0.f, 1.f}, 0xffff0000},
        {{0.f, 0.8f, 0.5f}, {0.f, 0.f, 1.f}, 0xffff0000},
        {{0.8f, -0.8f, 0.5f}, {0.f, 0.f, 1.f}, 0xffff0000},
        {{0.8f, 0.8f, 0.5f}, {0.f, 0.f, 1.f}, 0xffff0000}
    }};
    const std::array<std::uint16_t, 4> i16 = {0,1,2,3};
    const std::array<std::uint32_t, 4> i32 = {0,1,2,3};
    void * a = MapVertex(red, 0, sizeof(vRed), RENDER_MAP_NONE);
    void * b = MapVertex(green, 0, sizeof(vGreen), RENDER_MAP_NONE);
    void * e = MapVertex(lit, 0, sizeof(vLit), RENDER_MAP_NONE);
    void * c = MapIndex(indices16, 0, sizeof(i16), RENDER_MAP_NONE);
    void * d = MapIndex(indices32, 0, sizeof(i32), RENDER_MAP_NONE);
    if (!Check(a && b && c && d && e, "Map W3D resource shadows")) return 1;
    std::memcpy(a, vRed.data(), sizeof(vRed));
    std::memcpy(b, vGreen.data(), sizeof(vGreen));
    std::memcpy(e, vLit.data(), sizeof(vLit));
    std::memcpy(c, i16.data(), sizeof(i16));
    std::memcpy(d, i32.data(), sizeof(i32));
    if (!Check(UnmapVertex(red) && UnmapVertex(green) && UnmapVertex(lit) &&
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
        // DX12PrimaryWindow applies the camera viewport in the real game.
        // This isolated device-level smoke must establish its own RS state.
        const D3D12_VIEWPORT viewport = {0, 0, 160.f, 120.f, 0.f, 1.f};
        const D3D12_RECT scissor = {0, 0, 160, 120};
        device.ActiveCommandList()->RSSetViewports(1, &viewport);
        device.ActiveCommandList()->RSSetScissorRects(1, &scissor);
        draw.BeginFrame();
        draw.BindVertex(NativeVertexLayout(pass ? green : red), 4);
        draw.BindIndex(4);
        // Verify out-of-bounds draws are rejected rather than submitting bad
        // D3D12 index ranges that could remove a device.
        if (!Check(!draw.Draw(device, targets, textureStages, Identity, Identity,
                              false, 3, 2, 0, 4),
                   "reject out-of-range game draw"))
            return 1;
        if (!Check(draw.Draw(device, targets, textureStages, Identity, Identity,
                             pass != 0, 0, pass != 0 ? 2 : 1, 0, 4),
                   "draw raw FVF game geometry, no preview mesh", draw.LastError()) ||
            !Check(draw.DrawCount() == 1 && draw.PipelineCount() >= 1,
                   "native W3D PSO cache submitted exactly one draw"))
            return 1;
        if (pass == 0)
        {
            // A sorted transparent material may change blend equation and
            // per-channel masks between adjacent draws. PSO cache entries
            // must be distinct, and an all-disabled write mask may not
            // change the previously rendered WARP pixel.
            draw.SetSourceBlend(RENDER_BLEND_ONE);
            draw.SetDestinationBlend(RENDER_BLEND_ONE);
            draw.SetBlendOperation(RENDER_BLEND_OPERATION_REVERSE_SUBTRACT);
            draw.SetBlendingEnabled(true);
            draw.SetColorWriteMask(false, false);
            if (!Check(draw.Draw(device, targets, textureStages, Identity, Identity,
                                 false, 0, 1, 0, 4),
                       "sorted alpha state records a second native PSO",
                       draw.LastError()) ||
                !Check(draw.PipelineCount() >= 2,
                       "blend equation and color mask create separate native PSO"))
                return 1;
            draw.SetColorWriteMask(true, true);
            draw.SetBlendingEnabled(false);
            draw.SetDestinationBlend(RENDER_BLEND_ZERO);
            draw.SetBlendOperation(RENDER_BLEND_OPERATION_ADD);
        }
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

    // M01: light environment and material color sources must affect
    // real WARP pixels when the original FVF contains normal vectors.
    // The vertices are pure red; an enabled green directional light with
    // material-sourced white diffuse must output green, not the old red.
    // Reversing the light gives zero Lambert contribution (black).
    RenderLightingEnvironmentDescription lights;
    lights.lightCount = 1;
    lights.lights[0].direction[2] = 1.f;
    lights.lights[0].diffuse[1] = 1.f;
    RenderVertexMaterialDescription material;
    RenderMaterialColorSources sources;
    for (unsigned pass = 0; pass < 2; ++pass)
    {
        if (!Check(device.BeginFrame(clear), "begin lit W3D frame") ||
            !Check(targets.BeginFrame(device), "begin native lit render target") ||
            !Check(BindVertex(device, lit, 0, sizeof(LitVertex)),
                   "bind XYZ NORMAL DIFFUSE FVF", LastError()) ||
            !Check(BindIndex(device, indices16, 0), "bind lit triangle index", LastError()))
            return 1;
        const D3D12_VIEWPORT viewport = {0, 0, 160.f, 120.f, 0.f, 1.f};
        const D3D12_RECT scissor = {0, 0, 160, 120};
        device.ActiveCommandList()->RSSetViewports(1, &viewport);
        device.ActiveCommandList()->RSSetScissorRects(1, &scissor);
        draw.BeginFrame();
        draw.BindVertex(NativeVertexLayout(lit), 4);
        draw.BindIndex(4);
        draw.SetMaterial(material);
        draw.SetMaterialColorSources(sources);
        draw.SetLightingEnabled(true);
        lights.lights[0].direction[2] = pass == 0 ? 1.f : -1.f;
        draw.SetLightingEnvironment(lights);
        if (!Check(draw.Draw(device, targets, textureStages,
                             Identity, Identity, false, 0, 1, 0, 4),
                   "native W3D normal receives directional light", draw.LastError()))
            return 1;
        ComPtr<ID3D12Resource> readback;
        UINT rowPitch = 0;
        unsigned char unused[4] = {};
        if (!ReadPixel(device, unused, readback, rowPitch) ||
            !Check(targets.EndFrame(device), "end lit render target") ||
            !Check(device.EndFrame(0, false) && device.WaitForIdle(),
                   "fence-safe directional lighting submission", device.LastError()) ||
            !CheckReadback(readback.Get(), rowPitch, pass == 0 ? 1 : 3))
            return 1;
    }

    // M03: physically localized lighting is evaluated at each vertex,
    // rather than pre-attenuating a single directional light at mesh center.
    // The same original W3D FVF is tested with point range, spotlight cone
    // rejection, and red specular (added after the texture-stage cascade).
    const float eyeView[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, -100.f, 1.f
    };
    for (unsigned pass = 0; pass < 5; ++pass)
    {
        RenderLightingEnvironmentDescription local;
        local.lightCount = 1;
        auto & light = local.lights[0];
        light.type = pass == 2 || pass == 3 ? 2u : 1u;
        light.position[2] = pass == 1 ? 400.f : 5.5f;
        light.diffuse[1] = pass == 4 ? 0.f : 1.f;
        light.specular[0] = pass == 4 ? 1.f : 0.f;
        light.spotDirection[2] = pass == 3 ? 1.f : -1.f;
        light.attenuation[0] = 0.f;
        light.attenuation[1] = 50.f;
        light.attenuation[2] = 1.f;
        light.attenuation[3] = 0.5f;
        RenderVertexMaterialDescription localMaterial;
        if (pass == 4)
        {
            localMaterial.diffuse = RenderMaterialColor(0.f, 0.f, 0.f, 1.f);
            localMaterial.specular = RenderMaterialColor(1.f, 0.f, 0.f, 1.f);
            localMaterial.power = 8.f;
        }
        if (!Check(device.BeginFrame(clear), "begin native local-light frame") ||
            !Check(targets.BeginFrame(device), "attach local-light render targets") ||
            !Check(BindVertex(device, lit, 0, sizeof(LitVertex)),
                   "bind local-light W3D geometry", LastError()) ||
            !Check(BindIndex(device, indices16, 0), "bind local-light indices", LastError()))
            return 1;
        const D3D12_VIEWPORT viewport = {0, 0, 160.f, 120.f, 0.f, 1.f};
        const D3D12_RECT scissor = {0, 0, 160, 120};
        device.ActiveCommandList()->RSSetViewports(1, &viewport);
        device.ActiveCommandList()->RSSetScissorRects(1, &scissor);
        draw.BeginFrame();
        draw.BindVertex(NativeVertexLayout(lit), 4);
        draw.BindIndex(4);
        draw.SetMaterial(localMaterial);
        draw.SetMaterialColorSources(sources);
        draw.SetLightingEnvironment(local);
        draw.SetSpecularEnabled(pass == 4);
        if (!Check(draw.Draw(device, targets, textureStages, Identity, Identity,
                             false, 0, 1, 0, 4, Identity,
                             pass == 4 ? eyeView : Identity),
                   "shade W3D point, spot or material specular", draw.LastError()))
            return 1;
        ComPtr<ID3D12Resource> readback;
        UINT rowPitch = 0;
        unsigned char unused[4] = {};
        const unsigned expected = pass == 1 || pass == 3 ? 3u : pass == 4 ? 0u : 1u;
        if (!ReadPixel(device, unused, readback, rowPitch) ||
            !Check(targets.EndFrame(device), "end local-light target") ||
            !Check(device.EndFrame(0, false) && device.WaitForIdle(),
                   "submit and fence point/spot/specular draw", device.LastError()) ||
            !CheckReadback(readback.Get(), rowPitch, expected))
            return 1;
    }
    draw.SetSpecularEnabled(false);
    draw.SetLightingEnabled(false);

    // R06/M01: exercise the *running* gameplay pixel shader (not just the
    // separate HLSL reference) with two real sampled W3D textures and
    // dynamically selected wrap/clamp samplers. Pixel readback catches
    // silently ignored detail stages and sampler state.
    struct TexturedVertex
    {
        float xyz[3];
        std::uint32_t diffuse;
        float uv0[2];
        float uv1[2];
    };
    RenderBufferDescription texturedDesc;
    texturedDesc.byteSize = sizeof(TexturedVertex) * 4;
    texturedDesc.legacyVertexFormat = 0x0242u; // FVF XYZ | DIFFUSE | TEX2
    auto * textured = CreateVertex(device.NativeDevice(), texturedDesc);
    RenderTexture2DDescription imageDesc;
    imageDesc.width = imageDesc.height = 2;
    imageDesc.pixelFormat = 2; // original W3D A8R8G8B8
    imageDesc.mipLevels = 1;
    auto tex0 = DX12W3DTextures::Create2D(device.NativeDevice(), imageDesc);
    auto tex1 = DX12W3DTextures::Create2D(device.NativeDevice(), imageDesc);
    if (!Check(textured && tex0 && tex1, "create 2-UV geometry and real native W3D textures"))
        return 1;
    const std::array<TexturedVertex, 4> texturedVertices = {{
        {{-0.8f, -0.8f, 0.5f}, 0xff00ff00, {1.25f, 0.25f}, {0.25f, 0.25f}},
        {{0.f, 0.8f, 0.5f}, 0xff00ff00, {1.25f, 0.25f}, {0.25f, 0.25f}},
        {{0.8f, -0.8f, 0.5f}, 0xff00ff00, {1.25f, 0.25f}, {0.25f, 0.25f}},
        {{0.8f, 0.8f, 0.5f}, 0xff00ff00, {1.25f, 0.25f}, {0.25f, 0.25f}}
    }};
    void * texGeometry = MapVertex(textured, 0, sizeof(texturedVertices), RENDER_MAP_NONE);
    if (!Check(texGeometry != nullptr, "map textured FVF geometry")) return 1;
    std::memcpy(texGeometry, texturedVertices.data(), sizeof(texturedVertices));
    if (!Check(UnmapVertex(textured), "unmap two-UV geometry")) return 1;
    RenderSurfaceMap map0, map1;
    if (!Check(DX12W3DTextures::Map2D(tex0, 0, map0) &&
               DX12W3DTextures::Map2D(tex1, 0, map1),
               "map original format native textures")) return 1;
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 2; ++x)
        {
            unsigned char primary[4] = {0, 0, 0, 255};
            primary[0] = x == 0 ? 0 : 255;
            primary[2] = x == 0 ? 255 : 0;
            const unsigned char detail[4] = {0u, 255u, 0u, 255u};
            std::memcpy(static_cast<unsigned char *>(map0.data) + y * map0.rowPitch + 4*x,
                        primary, sizeof(primary));
            std::memcpy(static_cast<unsigned char *>(map1.data) + y * map1.rowPitch + 4*x,
                        detail, sizeof(detail));
        }
    if (!Check(DX12W3DTextures::Unmap2D(tex0, 0) &&
               DX12W3DTextures::Unmap2D(tex1, 0), "upload two test texture mip shadows"))
        return 1;
    RenderTextureHandle handle0 = reinterpret_cast<RenderTextureHandle>(tex0);
    RenderTextureHandle handle1 = reinterpret_cast<RenderTextureHandle>(tex1);
    if (!Check(textureStages.Bind(0, handle0) && textureStages.Bind(1, handle1),
               "bind two authoritative W3D texture handles")) return 1;
    const RenderTextureStageChannel selectTex(RENDER_TEXOP_SELECT_ARG1,
        RENDER_TEXARG_TEXTURE, RENDER_TEXARG_CURRENT);
    const RenderTextureStageChannel modulateTex(RENDER_TEXOP_MODULATE,
        RENDER_TEXARG_TEXTURE, RENDER_TEXARG_DIFFUSE);
    for (unsigned pass = 0; pass < 4; ++pass)
    {
        if (!Check(device.BeginFrame(clear), "begin texture combiner frame") ||
            !Check(targets.BeginFrame(device), "attach texture combiner targets") ||
            !Check(BindVertex(device, textured, 0, sizeof(TexturedVertex)),
                   "bind two-UV W3D vertices", LastError()) ||
            !Check(BindIndex(device, indices16, 0), "bind two-UV W3D indices", LastError()))
            return 1;
        const D3D12_VIEWPORT viewport = {0, 0, 160.f, 120.f, 0.f, 1.f};
        const D3D12_RECT scissor = {0, 0, 160, 120};
        device.ActiveCommandList()->RSSetViewports(1, &viewport);
        device.ActiveCommandList()->RSSetScissorRects(1, &scissor);
        draw.BeginFrame();
        draw.BindVertex(NativeVertexLayout(textured), 4);
        draw.BindIndex(4);
        draw.SetMaterial(RenderVertexMaterialDescription());
        draw.SetTextureChannel(0, false, pass == 2 ? modulateTex : selectTex);
        draw.SetTextureChannel(0, true, selectTex);
        draw.SetTextureChannel(1, false, pass == 3 ? selectTex :
            RenderTextureStageChannel());
        draw.SetTextureChannel(1, true, pass == 3 ? selectTex :
            RenderTextureStageChannel());
        if (!Check(textureStages.SetAddress(0, RENDER_SAMPLER_AXIS_U,
                      pass == 1 ? RENDER_SAMPLER_CLAMP : RENDER_SAMPLER_REPEAT),
                   "select native sampler wrap or clamp"))
            return 1;
        if (!Check(draw.Draw(device, targets, textureStages, Identity, Identity,
                             false, 0, 1, 0, 4),
                   "sample W3D combiner and dynamic sampler", draw.LastError()))
            return 1;
        ComPtr<ID3D12Resource> readback;
        UINT rowPitch = 0;
        unsigned char unused[4] = {};
        // wrap=>red; clamp=>blue; red*vertex-green=>black;
        // detail stage SELECT_TEXTURE=>green.
        const unsigned expected = pass == 0 ? 0u : pass == 1 ? 2u : pass == 2 ? 3u : 1u;
        if (!ReadPixel(device, unused, readback, rowPitch) ||
            !Check(targets.EndFrame(device), "end texture combiner target") ||
            !Check(device.EndFrame(0, false) && device.WaitForIdle(),
                   "submit native W3D textured draw", device.LastError()) ||
            !CheckReadback(readback.Get(), rowPitch, expected))
            return 1;
    }
    textureStages.Bind(0, nullptr);
    textureStages.Bind(1, nullptr);
    DX12W3DTextures::Release(handle0);
    DX12W3DTextures::Release(handle1);
    ReleaseVertex(textured);

    // B07: a real WARP readback of opaque followed by sorted-style
    // 50%-alpha geometry. This is not an in-game DX12-only comparison:
    // it isolates depth, draw ordering and the native PSO blend equation.
    if (!Check(device.BeginFrame(clear), "begin opaque and transparent frame") ||
        !Check(targets.BeginFrame(device), "bind opaque/transparent target") ||
        !Check(BindVertex(device, red, 0, sizeof(Vertex)),
               "bind opaque red geometry", LastError()) ||
        !Check(BindIndex(device, indices16, 0),
               "bind opaque index buffer", LastError()))
        return 1;
    const D3D12_VIEWPORT blendViewport = {0, 0, 160.f, 120.f, 0.f, 1.f};
    const D3D12_RECT blendScissor = {0, 0, 160, 120};
    device.ActiveCommandList()->RSSetViewports(1, &blendViewport);
    device.ActiveCommandList()->RSSetScissorRects(1, &blendScissor);
    draw.BeginFrame();
    draw.BindVertex(NativeVertexLayout(red), 4);
    draw.BindIndex(4);
    draw.SetMaterial(RenderVertexMaterialDescription());
    draw.SetBlend(RenderBlendState());
    draw.SetDepthWrite(true);
    if (!Check(draw.Draw(device, targets, textureStages, Identity, Identity,
                         false, 0, 1, 0, 4),
               "draw opaque base W3D mesh", draw.LastError()) ||
        !Check(BindVertex(device, green, 0, sizeof(Vertex)),
               "bind transparent green strip", LastError()))
        return 1;
    draw.BindVertex(NativeVertexLayout(green), 4);
    RenderVertexMaterialDescription halfOpacity;
    halfOpacity.diffuse.a = 0.5f;
    draw.SetMaterial(halfOpacity);
    RenderBlendState transparent;
    transparent.blendEnabled = true;
    transparent.source = RENDER_BLEND_SRC_ALPHA;
    transparent.destination = RENDER_BLEND_INV_SRC_ALPHA;
    draw.SetBlend(transparent);
    draw.SetDepthWrite(false);
    if (!Check(draw.Draw(device, targets, textureStages, Identity, Identity,
                         true, 0, 2, 0, 4),
               "composite sorted-style transparent native mesh", draw.LastError()))
        return 1;
    ComPtr<ID3D12Resource> blendReadback;
    UINT blendPitch = 0;
    unsigned char unusedBlend[4] = {};
    if (!ReadPixel(device, unusedBlend, blendReadback, blendPitch) ||
        !Check(targets.EndFrame(device), "end transparent blend target") ||
        !Check(device.EndFrame(0, false) && device.WaitForIdle(),
               "submit native opaque/transparent blend readback", device.LastError()))
        return 1;
    void * blendPixels = nullptr;
    if (!Check(SUCCEEDED(blendReadback->Map(0, nullptr, &blendPixels)) &&
               blendPixels != nullptr, "map opaque/transparent WARP pixel"))
        return 1;
    const auto * mixed = static_cast<const unsigned char *>(blendPixels) +
                         60 * blendPitch + 80 * 4;
    const bool halfBlend = mixed[0] > 110 && mixed[0] < 145 &&
                           mixed[1] > 110 && mixed[1] < 145 && mixed[2] < 16;
    if (!halfBlend)
        std::cerr << "M01 opaque/transparent pixel RGBA=(" <<
            unsigned(mixed[0]) << "," << unsigned(mixed[1]) << "," <<
            unsigned(mixed[2]) << "," << unsigned(mixed[3]) << ")\n";
    blendReadback->Unmap(0, nullptr);
    if (!Check(halfBlend, "native opaque plus 50% translucent W3D composite"))
        return 1;


    // U01 hardware raster test: a screen-space triangle clipped away from
    // the center must leave the clear color, even with a valid draw. A
    // center-containing clip must restore the red pixel; a disabled widget
    // converts that red pixel to luminance without changing its alpha.
    for (unsigned uiPass = 0; uiPass < 3; ++uiPass)
    {
        if (!Check(device.BeginFrame(clear), "begin native 2D clip/grayscale pass") ||
            !Check(targets.BeginFrame(device), "bind 2D target") ||
            !Check(BindVertex(device, red, 0, sizeof(Vertex)),
                   "bind UI colored geometry", LastError()) ||
            !Check(BindIndex(device, indices16, 0), "bind UI index buffer", LastError()))
            return 1;
        const D3D12_VIEWPORT viewport = {0, 0, 160.f, 120.f, 0.f, 1.f};
        const D3D12_RECT excluded = {0, 0, 40, 120};
        const D3D12_RECT included = {60, 40, 100, 80};
        device.ActiveCommandList()->RSSetViewports(1, &viewport);
        device.ActiveCommandList()->RSSetScissorRects(
            1, uiPass == 0 ? &excluded : &included);
        draw.BeginFrame();
        draw.BindVertex(NativeVertexLayout(red), 4);
        draw.BindIndex(4);
        draw.SetMaterial(RenderVertexMaterialDescription());
        draw.SetBlend(RenderBlendState());
        draw.SetCull(RENDER_CULL_NONE);
        draw.SetDepthCompare(RENDER_COMPARE_ALWAYS);
        draw.SetDepthWrite(false);
        draw.SetLightingEnabled(false);
        draw.SetSpecularEnabled(false);
        draw.SetGrayscaleEnabled(uiPass == 2);
        if (!Check(draw.Draw(device, targets, textureStages, Identity, Identity,
                             false, 0, 1, 0, 4),
                   "draw clipped/tinted screen-space UI primitive", draw.LastError()))
            return 1;
        ComPtr<ID3D12Resource> uiReadback;
        UINT uiPitch = 0;
        unsigned char unusedUI[4] = {};
        if (!ReadPixel(device, unusedUI, uiReadback, uiPitch) ||
            !Check(targets.EndFrame(device), "end native 2D UI target") ||
            !Check(device.EndFrame(0, false) && device.WaitForIdle(),
                   "submit clipped UI draw and fence readback", device.LastError()))
            return 1;
        void * uiPixels = nullptr;
        if (!Check(SUCCEEDED(uiReadback->Map(0, nullptr, &uiPixels)) &&
                   uiPixels != nullptr, "map 2D clipped WARP pixel"))
            return 1;
        const auto * px = static_cast<const unsigned char *>(uiPixels) +
                          60 * uiPitch + 80 * 4;
        const bool correct = uiPass == 0 ?
            (px[0] >= 10 && px[0] <= 24 &&
             px[1] >= 10 && px[1] <= 24 &&
             px[2] >= 10 && px[2] <= 24) :
            uiPass == 1 ? (px[0] > 230 && px[1] < 10 && px[2] < 10) :
            (px[0] >= 70 && px[0] <= 82 &&
             px[1] >= 70 && px[1] <= 82 &&
             px[2] >= 70 && px[2] <= 82);
        if (!correct)
            std::cerr << "U01 clipped/grayscale WARP pixel pass " << uiPass <<
                " RGBA=(" << unsigned(px[0]) << "," << unsigned(px[1]) <<
                "," << unsigned(px[2]) << "," << unsigned(px[3]) << ")\n";
        uiReadback->Unmap(0, nullptr);
        if (!Check(correct, "U01 scissor and luminance parity via GPU readback"))
            return 1;
    }
    draw.SetGrayscaleEnabled(false);

    // R08: the *actual* W3D R07 offscreen RTV is rendered, copied back
    // synchronously while its frame is still active, then drawn again.
    // Validate GPU->CPU, CPU->GPU and GPU->GPU transfers on the same frame,
    // including command allocator reset and fence-safe IA/R07 restoration.
    {
        DX12W3DSurfaceTransfers transfers;
        RenderSurfaceDescription texDesc;
        texDesc.width = 160; texDesc.height = 120; texDesc.pixelFormat = 2;
        RenderTexture2DHandle texA = targets.CreateOffscreenTexture(device, texDesc);
        RenderTexture2DHandle texB = targets.CreateOffscreenTexture(device, texDesc);
        if (!Check(texA && texB, "allocate R08 real GPU render targets")) return 1;
        RenderSurfaceHandle aSurface = targets.AcquireTextureLevel(device, texA, 0);
        RenderSurfaceHandle bSurface = targets.AcquireTextureLevel(device, texB, 0);
        RenderSurfaceHandle cpuSurface = DX12W3DSurfaces::Create(texDesc);
        if (!Check(aSurface && bSurface && cpuSurface,
                   "acquire R08 GPU and CPU surface handles")) return 1;
        if (!Check(device.BeginFrame(clear), "begin R08 native transfer frame") ||
            !Check(targets.BeginFrame(device), "begin R08 target ownership") ||
            !Check(targets.Bind(device, aSurface, nullptr), "bind R08 offscreen RTV") ||
            !Check(BindVertex(device, red, 0, sizeof(Vertex)),
                   "bind R08 colored mesh", LastError()) ||
            !Check(BindIndex(device, indices16, 0), "bind R08 W3D indices", LastError()))
            return 1;
        const D3D12_VIEWPORT viewport = {0, 0, 160, 120, 0, 1};
        const D3D12_RECT scissors = {0, 0, 160, 120};
        device.ActiveCommandList()->RSSetViewports(1, &viewport);
        device.ActiveCommandList()->RSSetScissorRects(1, &scissors);
        draw.BeginFrame();
        draw.BindVertex(NativeVertexLayout(red), 4);
        draw.BindIndex(4);
        draw.SetBlend(RenderBlendState());
        draw.SetCull(RENDER_CULL_NONE);
        draw.SetGrayscaleEnabled(false);
        if (!Check(draw.Draw(device, targets, textureStages, Identity, Identity,
                             false, 0, 1, 0, 4), "draw R08 GPU source", draw.LastError()))
            return 1;

        const RenderSurfaceRect center = {80, 60, 81, 61};
        unsigned char rgba[4] = {};
        if (!Check(transfers.ReadRegion(device, targets, aSurface, rgba, 4, center) == 4 &&
                   rgba[0] == 0 && rgba[1] == 0 && rgba[2] > 230,
                   "R08 in-frame GPU readback sees completed opaque source",
                   transfers.LastError()))
            return 1;

        // Drawing after the flush proves the R07 OM and R01 IA state has
        // been rebound on the freshly reset command list.
        if (!Check(BindVertex(device, green, 0, sizeof(Vertex)),
                   "bind R08 post-flush green mesh", LastError())) return 1;
        draw.BindVertex(NativeVertexLayout(green), 4);
        if (!Check(draw.Draw(device, targets, textureStages, Identity, Identity,
                             true, 0, 2, 0, 4),
                   "draw same frame after R08 flush", draw.LastError()))
            return 1;
        if (!Check(transfers.ReadRegion(device, targets, aSurface, rgba, 4, center) == 4 &&
                   rgba[1] > 230 && rgba[0] < 12 && rgba[2] < 12,
                   "R08 second draw survives flush and shows green", transfers.LastError()))
            return 1;
        const RenderSurfacePoint zero = {0, 0};
        if (!Check(transfers.Copy(device, targets, aSurface, nullptr,
                                  cpuSurface, &zero),
                   "R08 GPU to CPU full-surface copy", transfers.LastError()))
            return 1;
        RenderSurfaceMap cpuMap;
        if (!Check(DX12W3DSurfaces::Lock(cpuSurface, cpuMap, &center, true),
                   "map R08 CPU copy", DX12W3DSurfaces::LastError())) return 1;
        const auto * cpuPixel = static_cast<const unsigned char *>(cpuMap.data);
        const bool correctCPU = cpuPixel[1] > 230 && cpuPixel[0] < 12 && cpuPixel[2] < 12;
        DX12W3DSurfaces::Unlock(cpuSurface);
        if (!Check(correctCPU, "R08 GPU image retains BGRA pixel in CPU lock")) return 1;
        if (!Check(transfers.Copy(device, targets, aSurface, nullptr, bSurface, &zero),
                   "R08 native GPU to GPU CopyTextureRegion", transfers.LastError()))
            return 1;
        if (!Check(transfers.ReadRegion(device, targets, bSurface, rgba, 4, center) == 4 &&
                   rgba[1] > 230, "R08 copied destination WARP pixel", transfers.LastError()))
            return 1;

        // RMW surface lock writes the exact region back to a native RTT.
        RenderSurfaceMap gpuMap;
        if (!Check(transfers.Lock(device, targets, bSurface, gpuMap, &center, false),
                   "R08 GPU surface read-modify-write lock", transfers.LastError()))
            return 1;
        auto * edit = static_cast<unsigned char *>(gpuMap.data);
        edit[0] = 255; edit[1] = 0; edit[2] = 0; edit[3] = 255; // blue BGRA
        if (!Check(transfers.Unlock(device, targets, bSurface) &&
                   transfers.ReadRegion(device, targets, bSurface, rgba, 4, center) == 4 &&
                   rgba[0] > 230 && rgba[1] < 12,
                   "R08 GPU surface unlock submits edited pixel", transfers.LastError()))
            return 1;
        // CPU-to-GPU scaled TRIANGLE blit exercises bilinear sampling,
        // upload footprints and preserved pixels outside its destination.
        RenderSurfaceDescription tileDesc;
        tileDesc.width = tileDesc.height = 2;
        tileDesc.pixelFormat = 2;
        const auto tile = DX12W3DSurfaces::Create(tileDesc);
        RenderSurfaceMap tileMap;
        if (!Check(tile && DX12W3DSurfaces::Lock(tile, tileMap),
                   "map R08 bilinear source tile")) return 1;
        const unsigned char checker[16] = {
            0,0,255,255, 0,255,0,255,
            255,0,0,255, 255,255,255,255
        };
        for (unsigned y = 0; y < 2; ++y)
            std::memcpy(static_cast<unsigned char *>(tileMap.data) +
                        y * tileMap.rowPitch, checker + y * 8, 8);
        DX12W3DSurfaces::Unlock(tile);
        const RenderSurfaceRect srcTile = {0,0,2,2};
        const RenderSurfaceRect stretch = {70,50,90,70};
        if (!Check(transfers.Blit(device, targets, tile, srcTile, bSurface, stretch,
                                  RENDER_SURFACE_BLIT_TRIANGLE),
                   "R08 bilinear CPU surface to GPU blit", transfers.LastError()))
            return 1;
        if (!Check(transfers.ReadRegion(device, targets, bSurface, rgba, 4, center) == 4 &&
                   rgba[0] > 75 && rgba[0] < 185 &&
                   rgba[1] > 75 && rgba[1] < 185 &&
                   rgba[2] > 75 && rgba[2] < 185,
                   "R08 half-pixel bilinear native output", transfers.LastError()))
            return 1;
        DX12W3DSurfaces::Release(tile);
        if (!Check(targets.Bind(device, nullptr, nullptr) &&
                   targets.EndFrame(device) && device.EndFrame(0, false) &&
                   device.WaitForIdle(), "fence R08 surface copy tests", targets.LastError()))
            return 1;
        DX12W3DSurfaces::Release(cpuSurface);
        targets.Release(aSurface); targets.Release(bSurface);
        DX12W3DTextures::Release(reinterpret_cast<RenderTextureHandle>(texA));
        DX12W3DTextures::Release(reinterpret_cast<RenderTextureHandle>(texB));
    }

    ReleaseVertex(lit);
    ReleaseVertex(red);
    ReleaseVertex(green);
    ReleaseIndex(indices16);
    ReleaseIndex(indices32);
    device.Shutdown();
    std::cout << "M01 PASS: FVF indexed triangles/strips, directional normal lighting and alpha mask PSOs via WARP readback\n";
    return 0;
}
