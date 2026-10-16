/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/

// R03: W3D FVF decode parity, native D3D12 input-layout PSO, and a real
// WARP DrawIndexedInstanced using 32-bit indices and raw packed D3DCOLOR.
#include "../DX12PresentationDevice.h"
#include "../DX12W3DGeometryBuffers.h"
#include "../DX12W3DVertexLayouts.h"
#include <d3dcompiler.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

using Microsoft::WRL::ComPtr;
using namespace DX12W3DGeometryBuffers;

namespace
{
    bool Check(bool result, const char * operation)
    {
        if (!result)
            std::cerr << "DX12-R03: " << operation << " failed: "
                      << LastError() << '\n';
        return result;
    }

    struct TestWindow
    {
        HWND hwnd = nullptr;
        HINSTANCE instance = GetModuleHandleW(nullptr);
        bool registered = false;
        bool Create()
        {
            WNDCLASSW cls = {};
            cls.hInstance = instance;
            cls.lpfnWndProc = DefWindowProcW;
            cls.lpszClassName = L"W3DDX12R03WARP";
            registered = RegisterClassW(&cls) != 0;
            if (!registered)
                return false;
            hwnd = CreateWindowExW(0, cls.lpszClassName, L"DX12 R03 formats",
                                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                    320, 200, nullptr, nullptr, instance, nullptr);
            if (!hwnd)
                return false;
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            return true;
        }
        ~TestWindow()
        {
            if (hwnd && IsWindow(hwnd))
                DestroyWindow(hwnd);
            if (registered)
                UnregisterClassW(L"W3DDX12R03WARP", instance);
        }
    };

    bool Element(const DX12W3DVertexLayout & layout, unsigned i,
                 const char * semantic, unsigned semanticIndex,
                 unsigned offset, DXGI_FORMAT format)
    {
        if (i >= layout.elementCount)
            return false;
        const auto & x = layout.elements[i];
        return std::strcmp(x.SemanticName, semantic) == 0 &&
               x.SemanticIndex == semanticIndex &&
               x.AlignedByteOffset == offset && x.Format == format &&
               x.InputSlot == 0 &&
               x.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    }

    bool CheckLayouts()
    {
        struct Case { unsigned fvf, stride, elementCount; };
        constexpr Case cases[] = {
            {0x0002, 12, 1}, // XYZ
            {0x0012, 24, 2}, // XYZ|NORMAL
            {0x0112, 32, 3}, // XYZ|NORMAL|TEX1
            {0x0212, 40, 4}, // XYZ|NORMAL|TEX2
            {0x0252, 44, 5}, // XYZ|NORMAL|DIFFUSE|TEX2
            {0x02c2, 36, 5}, // XYZ|DIFFUSE|SPECULAR|TEX2
            {0x0142, 24, 3}, // XYZ|DIFFUSE|TEX1
            {0x0244, 36, 4}, // XYZRHW|DIFFUSE|TEX2
            {0x125c, 60, 7}, // XYZB4|LASTBETA_UBYTE4|NORMAL|DIFFUSE|TEX2
            {0x811e, 52, 5}, // XYZB5|LASTBETA_D3DCOLOR|NORMAL|TEX1
            {0x000e, 32, 3}, // XYZB5 with five separate float weights
            {0x1006, 16, 2}, // XYZB1|LASTBETA_UBYTE4 (zero weights)
            {0x4102, 24, 2}, // XYZW|TEX1
            {0x00022, 16, 2},// XYZ|PSIZE
            {0x540452, 72, 7}, // W3D tangent-space TEX2,TEX3,TEX3,TEX3
            {0xb0312, 52, 5}, // displacement TEX1,TEX4,TEX2
            {0x0802, 76, 9}  // XYZ|TEX8
        };
        for (const auto & test : cases)
        {
            DX12W3DVertexLayout layout;
            std::string reason;
            if (!DX12W3DVertexLayouts::DecodeLegacyFVF(test.fvf, layout, &reason) ||
                layout.stride != test.stride ||
                layout.elementCount != test.elementCount ||
                layout.NativeInputLayout().NumElements != test.elementCount)
            {
                std::cerr << "FVF " << std::hex << test.fvf << std::dec
                          << " decoder: stride=" << layout.stride
                          << " elements=" << layout.elementCount
                          << " error=" << reason << '\n';
                return false;
            }
        }
        DX12W3DVertexLayout layout;
        if (!DX12W3DVertexLayouts::DecodeLegacyFVF(0x125c, layout) ||
            !Element(layout, 1, "BLENDWEIGHT", 0, 12, DXGI_FORMAT_R32G32B32_FLOAT) ||
            !Element(layout, 2, "BLENDINDICES", 0, 24, DXGI_FORMAT_R8G8B8A8_UINT) ||
            !Element(layout, 3, "NORMAL", 0, 28, DXGI_FORMAT_R32G32B32_FLOAT) ||
            !Element(layout, 4, "COLOR", 0, 40, DXGI_FORMAT_R8G8B8A8_UNORM) ||
            !Element(layout, 5, "TEXCOORD", 0, 44, DXGI_FORMAT_R32G32_FLOAT) ||
            !Element(layout, 6, "TEXCOORD", 1, 52, DXGI_FORMAT_R32G32_FLOAT))
            return false;
        if (!DX12W3DVertexLayouts::DecodeLegacyFVF(0x02c2, layout) ||
            !Element(layout, 1, "COLOR", 0, 12, DXGI_FORMAT_R8G8B8A8_UNORM) ||
            !Element(layout, 2, "COLOR", 1, 16, DXGI_FORMAT_R8G8B8A8_UNORM) ||
            !Element(layout, 3, "TEXCOORD", 0, 20, DXGI_FORMAT_R32G32_FLOAT) ||
            !Element(layout, 4, "TEXCOORD", 1, 28, DXGI_FORMAT_R32G32_FLOAT) ||
            !layout.packedColorNeedsBGRASwizzle)
            return false;
        if (!DX12W3DVertexLayouts::DecodeLegacyFVF(0xb0312, layout) ||
            !Element(layout, 2, "TEXCOORD", 0, 24, DXGI_FORMAT_R32_FLOAT) ||
            !Element(layout, 3, "TEXCOORD", 1, 28, DXGI_FORMAT_R32G32B32A32_FLOAT) ||
            !Element(layout, 4, "TEXCOORD", 2, 44, DXGI_FORMAT_R32G32_FLOAT))
            return false;
        if (!DX12W3DVertexLayouts::DecodeLegacyFVF(0x540452, layout) ||
            !Element(layout, 3, "TEXCOORD", 0, 28, DXGI_FORMAT_R32G32_FLOAT) ||
            !Element(layout, 4, "TEXCOORD", 1, 36, DXGI_FORMAT_R32G32B32_FLOAT) ||
            !Element(layout, 5, "TEXCOORD", 2, 48, DXGI_FORMAT_R32G32B32_FLOAT) ||
            !Element(layout, 6, "TEXCOORD", 3, 60, DXGI_FORMAT_R32G32B32_FLOAT))
            return false;
        if (!DX12W3DVertexLayouts::DecodeLegacyFVF(0x0244, layout) ||
            !layout.transformed ||
            !Element(layout, 0, "POSITION", 0, 0, DXGI_FORMAT_R32G32B32A32_FLOAT))
            return false;
        if (!DX12W3DVertexLayouts::DecodeLegacyFVF(0x000e, layout) ||
            !Element(layout, 1, "BLENDWEIGHT", 0, 12, DXGI_FORMAT_R32G32B32A32_FLOAT) ||
            !Element(layout, 2, "BLENDWEIGHT", 1, 28, DXGI_FORMAT_R32_FLOAT))
            return false;
        const unsigned invalid[] = {
            0x0000, 0x2002, 0x0902, 0x0014, 0x9006, 0x1002,
            0x4004, 0x10002, 0x0402
        };
        for (unsigned fvf : invalid)
        {
            std::string reason;
            if (DX12W3DVertexLayouts::DecodeLegacyFVF(fvf, layout, &reason) ||
                reason.empty() || layout.elementCount != 0)
            {
                std::cerr << "Invalid FVF accepted: " << std::hex << fvf << '\n';
                return false;
            }
        }
        return true;
    }

    struct Vertex
    {
        float position[3];
        float normal[3];
        std::uint32_t diffuse;
        float uv0[2];
        float uv1[2];
    };
    static_assert(sizeof(Vertex) == 44, "W3D FVF XYZ|NORMAL|DIFFUSE|TEX2 stride");

    bool ReadbackBuffer(ID3D12Device * device, UINT64 bytes,
                        ComPtr<ID3D12Resource> & result)
    {
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.SampleDesc.Count = 1;
        return SUCCEEDED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result)));
    }

    bool Compile(const char * source, const char * entry, const char * target,
                 ComPtr<ID3DBlob> & code)
    {
        ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(source, std::strlen(source), "R03", nullptr,
                                      nullptr, entry, target, 0, 0, &code, &errors);
        if (FAILED(hr) && errors)
            std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << '\n';
        return SUCCEEDED(hr);
    }

    bool DrawR32IndexMesh(DX12PresentationDevice & device)
    {
        const std::array<Vertex, 3> vertices = {{
            {{-0.7f, -0.7f, 0.5f}, {0, 0, 1}, 0xffff0000u, {0, 0}, {0, 0}},
            {{ 0.0f,  0.7f, 0.5f}, {0, 0, 1}, 0xffff0000u, {0, 0}, {0, 0}},
            {{ 0.7f, -0.7f, 0.5f}, {0, 0, 1}, 0xffff0000u, {0, 0}, {0, 0}}
        }};
        const std::array<std::uint32_t, 3> indices = {0, 1, 2};
        RenderBufferDescription vbDesc;
        vbDesc.legacyVertexFormat = 0x0252; // XYZ|NORMAL|DIFFUSE|TEX2
        vbDesc.byteSize = static_cast<unsigned>(sizeof(vertices));
        RenderBufferDescription ibDesc;
        ibDesc.byteSize = static_cast<unsigned>(sizeof(indices));
        ibDesc.indexFormat = RENDER_INDEX_UINT32;

        if (!Check(ibDesc.indexFormat == RENDER_INDEX_UINT32 &&
                   RenderBufferDescription().indexFormat == RENDER_INDEX_UINT16,
                   "Explicit 32-bit indices preserve legacy 16-bit default"))
            return false;
        auto * vertex = CreateVertex(device.NativeDevice(), vbDesc);
        auto * index = CreateIndex(device.NativeDevice(), ibDesc);
        if (!Check(vertex != nullptr && index != nullptr, "Create typed FVF/R32 geometry"))
            return false;
        auto invalid = ibDesc;
        invalid.byteSize = 6;
        if (!Check(!CreateIndex(device.NativeDevice(), invalid),
                   "Reject partial R32 indices"))
            return false;
        if (!Check(NativeVertexLayout(vertex) != nullptr &&
                   NativeVertexLayout(vertex)->stride == sizeof(Vertex) &&
                   NativeIndexElementBytes(index) == 4, "Expose FVF and R32 view metadata"))
            return false;
        void * vb = MapVertex(vertex, 0, vbDesc.byteSize, RENDER_MAP_NONE);
        void * ib = MapIndex(index, 0, ibDesc.byteSize, RENDER_MAP_NONE);
        if (!Check(vb && ib, "Map raw FVF vertices and R32 indices"))
            return false;
        std::memcpy(vb, vertices.data(), sizeof(vertices));
        std::memcpy(ib, indices.data(), sizeof(indices));
        if (!Check(UnmapVertex(vertex) && UnmapIndex(index), "Commit geometry"))
            return false;

        constexpr char shader[] = R"(
            struct VSIn {
                float3 position : POSITION;
                float3 normal : NORMAL;
                float4 diffuse : COLOR0;
                float2 uv0 : TEXCOORD0;
                float2 uv1 : TEXCOORD1;
            };
            struct VSOut {
                float4 position : SV_POSITION;
                float4 diffuse : COLOR0;
            };
            VSOut VSMain(VSIn v) {
                VSOut o;
                float correction = (v.normal.x + v.uv0.x + v.uv1.y) * 0.000001f;
                o.position = float4(v.position.x + correction, v.position.y,
                                    v.position.z, 1.0f);
                o.diffuse = v.diffuse.bgra; // D3DCOLOR packed BGRA in CPU memory
                return o;
            }
            float4 PSMain(VSOut v) : SV_TARGET0 { return v.diffuse; }
        )";
        ComPtr<ID3DBlob> vs, ps, serialized;
        if (!Check(Compile(shader, "VSMain", "vs_5_1", vs) &&
                   Compile(shader, "PSMain", "ps_5_1", ps), "Compile FVF VS/PS"))
            return false;
        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> rootErrors;
        if (!Check(SUCCEEDED(D3D12SerializeRootSignature(
                       &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &rootErrors)),
                   "Serialize IA root signature"))
            return false;
        ComPtr<ID3D12RootSignature> root;
        if (!Check(SUCCEEDED(device.NativeDevice()->CreateRootSignature(
                       0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                       IID_PPV_ARGS(&root))), "Create IA root signature"))
            return false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = root.Get();
        psoDesc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        psoDesc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        psoDesc.InputLayout = NativeVertexLayout(vertex)->NativeInputLayout();
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        ComPtr<ID3D12PipelineState> pipeline;
        if (!Check(SUCCEEDED(device.NativeDevice()->CreateGraphicsPipelineState(
                       &psoDesc, IID_PPV_ARGS(&pipeline))),
                   "Create native PSO from decoded W3D FVF layout"))
            return false;

        const float clear[] = {0.08f, 0.08f, 0.15f, 1.0f};
        if (!Check(device.BeginFrame(clear), "Begin WARP FVF render") ||
            !Check(BindVertex(device, vertex, 0, sizeof(Vertex)), "Bind raw FVF stream") ||
            !Check(BindIndex(device, index, 0), "Bind R32_UINT indexed stream"))
            return false;

        auto * cmd = device.ActiveCommandList();
        cmd->SetGraphicsRootSignature(root.Get());
        cmd->SetPipelineState(pipeline.Get());
        const D3D12_VIEWPORT viewport = {0, 0, 160, 120, 0, 1};
        const D3D12_RECT scissor = {0, 0, 160, 120};
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);
        const auto rtv = device.CurrentBackBufferRTV();
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawIndexedInstanced(3, 1, 0, 0, 0);

        ID3D12Resource * backBuffer = device.CurrentBackBuffer();
        const auto desc = backBuffer->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT rows = 0;
        UINT64 rowBytes = 0, totalBytes = 0;
        device.NativeDevice()->GetCopyableFootprints(
            &desc, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);
        ComPtr<ID3D12Resource> readback, indexReadback;
        if (!Check(ReadbackBuffer(device.NativeDevice(), totalBytes, readback) &&
                   ReadbackBuffer(device.NativeDevice(), sizeof(indices), indexReadback),
                   "Create WARP GPU readback resources"))
            return false;
        if (!Check(device.TransitionFrameResource(backBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE) &&
                   device.TransitionFrameResource(NativeIndexResource(index),
                                                 D3D12_RESOURCE_STATE_COPY_SOURCE),
                   "Barrier color/index resources to readback"))
            return false;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = backBuffer;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        cmd->CopyBufferRegion(indexReadback.Get(), 0, NativeIndexResource(index),
                              NativeIndexOffset(index), sizeof(indices));
        if (!Check(device.TransitionFrameResource(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET) &&
                   device.TransitionFrameResource(NativeIndexResource(index),
                                                 D3D12_RESOURCE_STATE_INDEX_BUFFER),
                   "Restore tracked backbuffer and index states"))
            return false;

        // The render frame fence keeps both allocations alive after release.
        ReleaseVertex(vertex);
        ReleaseIndex(index);
        if (!Check(device.EndFrame(0, true) && device.WaitForIdle(),
                   "Submit indexed R32 triangle and wait for WARP fence"))
            return false;

        const D3D12_RANGE range = {0, static_cast<SIZE_T>(totalBytes)};
        void * raw = nullptr;
        if (!Check(SUCCEEDED(readback->Map(0, &range, &raw)) && raw,
                   "Map R32-indexed GPU render target"))
            return false;
        const auto * pixel = static_cast<const unsigned char *>(raw) +
                            60 * footprint.Footprint.RowPitch + 80 * 4;
        const bool red = pixel[0] > 180 && pixel[1] < 75 && pixel[2] < 75;
        const D3D12_RANGE empty = {0, 0};
        readback->Unmap(0, &empty);

        const D3D12_RANGE indexRange = {0, sizeof(indices)};
        void * readIndices = nullptr;
        if (!Check(SUCCEEDED(indexReadback->Map(0, &indexRange, &readIndices)) &&
                   readIndices, "Map R32 index readback"))
            return false;
        const bool equalIndices =
            std::memcmp(readIndices, indices.data(), sizeof(indices)) == 0;
        indexReadback->Unmap(0, &empty);
        return Check(red && equalIndices,
                     "WARP renders packed-BGRA W3D vertex with R32 indices");
    }
}

int wmain()
{
    if (!Check(CheckLayouts(), "Verify real W3D FVF offsets, formats and rejected variants"))
        return 1;
    TestWindow window;
    if (!Check(window.Create(), "Create smoke HWND"))
        return 1;
    DX12PresentationDevice device;
    if (!Check(device.Initialize(window.hwnd, 160, 120, true), "Initialize WARP device"))
        return 1;
    if (!DrawR32IndexMesh(device))
        return 1;
    device.Shutdown();
    std::cout << "DX12-R03 PASS: FVF layouts and native WARP R32_UINT indexed draw\n";
    return 0;
}
