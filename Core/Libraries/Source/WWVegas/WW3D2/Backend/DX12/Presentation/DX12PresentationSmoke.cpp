// Windowed WARP swap-chain test against the reusable W3D DX12 device.
// This must present real frames, synchronize/reuse back buffers, and survive
// resizing. A copy before Present verifies the clear on an actual GPU buffer.

#include "../DX12PresentationDevice.h"
#include "../DX12IndexedMeshPass.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr wchar_t WindowClassName[] = L"W3DDX12PresentationSmokeWindow";
    constexpr UINT CopyAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

    struct TestWindow
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND window = nullptr;
        bool registered = false;

        bool Create()
        {
            WNDCLASSW cls = {};
            cls.hInstance = instance;
            cls.lpfnWndProc = DefWindowProcW;
            cls.lpszClassName = WindowClassName;
            registered = RegisterClassW(&cls) != 0;
            if (!registered)
                return false;
            window = CreateWindowExW(0, WindowClassName, L"W3D DX12 WARP Presentation",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320, 240,
                nullptr, nullptr, instance, nullptr);
            if (!window)
                return false;
            ShowWindow(window, SW_SHOWNOACTIVATE);
            UpdateWindow(window);
            return true;
        }

        void Pump()
        {
            MSG message = {};
            while (PeekMessageW(&message, window, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        ~TestWindow()
        {
            if (window)
                DestroyWindow(window);
            if (registered)
                UnregisterClassW(WindowClassName, instance);
        }
    };

    bool Check(HRESULT hr, const char * label)
    {
        if (SUCCEEDED(hr))
            return true;
        std::cerr << label << ": 0x" << std::hex
                  << static_cast<unsigned>(hr) << std::dec << "\n";
        return false;
    }

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

    bool PresentAndVerify(DX12PresentationDevice & device,
                          const std::array<float, 4> & color,
                          const std::function<bool()> & recordDraw = {},
                          const std::array<float, 4> * expectedInside = nullptr,
                          bool expectPillarbox = false)
    {
        const UINT width = device.Width();
        const UINT height = device.Height();
        const UINT pitch = (width * 4 + CopyAlignment - 1) & ~(CopyAlignment - 1);

        D3D12_HEAP_PROPERTIES readbackHeap = {};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
        readbackHeap.CreationNodeMask = 1;
        readbackHeap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = UINT64(pitch) * height;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> readback;
        if (!Check(device.NativeDevice()->CreateCommittedResource(
                       &readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)),
                   "Create readback buffer"))
            return false;

        if (!device.BeginFrame(color.data()))
        {
            std::cerr << device.LastError() << "\n";
            return false;
        }
        auto * list = device.ActiveCommandList();
        auto * target = device.CurrentBackBuffer();
        if (!list || !target || (recordDraw && !recordDraw()))
            return false;

        const auto toCopy = Transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &toCopy);

        D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = width;
        dst.PlacedFootprint.Footprint.Height = height;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = pitch;
        src.pResource = target;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        const auto toRender = Transition(target, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &toRender);
        if (!device.EndFrame(0) || !device.WaitForIdle())
        {
            std::cerr << device.LastError() << "\n";
            return false;
        }

        const D3D12_RANGE range = {0, SIZE_T(pitch) * height};
        void * mapped = nullptr;
        if (!Check(readback->Map(0, &range, &mapped), "Map frame readback"))
            return false;
        const auto * center = static_cast<const unsigned char *>(mapped) +
                             (height / 2) * pitch + (width / 2) * 4;
        std::array<unsigned char, 4> actual = {center[0], center[1], center[2], center[3]};
        std::array<unsigned char, 4> leftPixel = {};
        if (expectPillarbox)
        {
            // At 160x90 with a square source camera, x=20 is outside the
            // centered 90-pixel viewport but inside an unstretched quad.
            const auto * edge = static_cast<const unsigned char *>(mapped) +
                                (height / 2) * pitch + (width / 8) * 4;
            for (unsigned i = 0; i < 4; ++i)
                leftPixel[i] = edge[i];
        }
        readback->Unmap(0, nullptr);
        if (expectPillarbox)
        {
            for (unsigned i = 0; i < 4; ++i)
            {
                const int expected = static_cast<int>(std::lround(color[i] * 255.f));
                if (std::abs(int(leftPixel[i]) - expected) > 2)
                {
                    std::cerr << "Aspect-preserving pillarbox pixel " << i
                              << ": expected " << expected
                              << ", got " << unsigned(leftPixel[i]) << "\n";
                    return false;
                }
            }
        }

        const auto & expectedColor = expectedInside ? *expectedInside : color;
        for (unsigned i = 0; i < 4; ++i)
        {
            const int expected = static_cast<int>(std::lround(expectedColor[i] * 255.0f));
            if (std::abs(int(actual[i]) - expected) > 2)
            {
                std::cerr << "Swapchain pixel " << i << ": expected " << expected
                          << ", got " << unsigned(actual[i]) << "\n";
                return false;
            }
        }
        return true;
    }

    bool TestIndexedMesh(DX12PresentationDevice & device, TestWindow & window,
                         const wchar_t * shaderSource)
    {
        DX12W3DIndexedMeshPass pass;
        if (!pass.Initialize(device, shaderSource))
        {
            std::cerr << "W3D mesh pass: " << pass.LastError() << "\n";
            return false;
        }

        DX12W3DIndexedMesh mesh;
        DX12W3DIndexedMesh depthMesh;
        DX12W3DIndexedMesh translucentMesh;
        const DX12W3DVertex vertices[] = {
            {{-0.8f, -0.8f, 0.f}, {1.f, 0.25f, 0.5f, 1.f}, {0.f, 0.f}, {0.f, 0.f}},
            {{-0.8f,  0.8f, 0.f}, {1.f, 0.25f, 0.5f, 1.f}, {0.f, 0.f}, {0.f, 0.f}},
            {{ 0.8f, -0.8f, 0.f}, {1.f, 0.25f, 0.5f, 1.f}, {0.f, 0.f}, {0.f, 0.f}},
            {{ 0.8f,  0.8f, 0.f}, {1.f, 0.25f, 0.5f, 1.f}, {0.f, 0.f}, {0.f, 0.f}}
        };
        const std::uint16_t indices[] = {0, 1, 2, 2, 1, 3};
        DX12W3DVertex depthVertices[8] = {};
        for (unsigned i = 0; i < 4; ++i)
        {
            depthVertices[i] = vertices[i];
            depthVertices[i].position[2] = 0.2f;
            depthVertices[i].diffuse[0] = 0.f;
            depthVertices[i].diffuse[1] = 1.f;
            depthVertices[i].diffuse[2] = 0.f;
            depthVertices[i + 4] = vertices[i];
            depthVertices[i + 4].position[2] = 0.8f;
            depthVertices[i + 4].diffuse[0] = 0.f;
            depthVertices[i + 4].diffuse[1] = 0.f;
            depthVertices[i + 4].diffuse[2] = 1.f;
        }
        const std::uint16_t depthIndices[] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
        bool depthUploaded = false;
        DX12W3DVertex translucentVertices[4] = {};
        for (unsigned i = 0; i < 4; ++i)
        {
            translucentVertices[i] = vertices[i];
            translucentVertices[i].position[2] = 0.2f;
            translucentVertices[i].diffuse[0] = 1.f;
            translucentVertices[i].diffuse[1] = 0.f;
            translucentVertices[i].diffuse[2] = 0.f;
            translucentVertices[i].diffuse[3] = 0.5f;
        }
        bool translucentUploaded = false;
        const std::uint8_t texel0[] = {128, 255, 255, 255};
        const std::uint8_t texel1[] = {128, 255, 255, 255};
        unsigned texture0 = 0, texture1 = 0;
        bool uploaded = false;
        const float identity[] = {
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f
        };

        const auto uploadOnce = [&]() -> bool
        {
            if (uploaded)
                return true;
            // Validate index bounds before recording any GPU copy commands.
            DX12W3DIndexedMesh invalid;
            const std::uint16_t badIndices[] = {0, 1, 99};
            if (invalid.Upload(device, vertices, 4, badIndices, 3))
            {
                std::cerr << "W3D indexed mesh accepted an invalid index\n";
                return false;
            }
            if (!mesh.Upload(device, vertices, 4, indices, 6))
            {
                std::cerr << "W3D mesh upload: " << mesh.LastError() << "\n";
                return false;
            }
            if (!pass.UploadTextureRGBA8(texel0, 1, 1, texture0) ||
                !pass.UploadTextureRGBA8(texel1, 1, 1, texture1))
            {
                std::cerr << "W3D texture upload: " << pass.LastError() << "\n";
                return false;
            }
            uploaded = true;
            return true;
        };

        RenderTextureCombinerKey untextured;
        RenderTextureCombinerKey singleStage;
        singleStage.color[0] = RenderTextureStageChannel(
            RENDER_TEXOP_MODULATE, RENDER_TEXARG_TEXTURE, RENDER_TEXARG_DIFFUSE);
        singleStage.alpha[0] = RenderTextureStageChannel(
            RENDER_TEXOP_SELECT_ARG1, RENDER_TEXARG_TEXTURE, RENDER_TEXARG_CURRENT);
        RenderTextureCombinerKey twoStage = singleStage;
        twoStage.color[1] = RenderTextureStageChannel(
            RENDER_TEXOP_MODULATE, RENDER_TEXARG_CURRENT, RENDER_TEXARG_TEXTURE);
        if (untextured.Encode() != 0x10101010u ||
            singleStage.Encode() != 0x10101193u ||
            twoStage.Encode() != 0x10431193u)
        {
            std::cerr << "W3D CPU material key does not match HLSL encoding\n";
            return false;
        }

        RenderBlendState normal;
        RenderBlendState rejected;
        rejected.alphaTestEnabled = true;
        rejected.alphaCompare = RENDER_COMPARE_GREATER;
        rejected.alphaReference = 255;
        const std::array<float, 4> clear = {{0.f, 0.f, 0.f, 1.f}};
        const std::array<float, 4> singlePixel = {{128.f / 255.f, 0.25f, 0.5f, 1.f}};
        const std::array<float, 4> doublePixel = {{(128.f / 255.f) * (128.f / 255.f), 0.25f, 0.5f, 1.f}};
        const std::array<float, 4> plainPixel = {{1.f, 0.25f, 0.5f, 1.f}};

        const auto runMaterial = [&](const RenderTextureCombinerKey & combiner,
                                     const RenderBlendState & state,
                                     const std::array<float, 4> & expected,
                                     bool expectPillarbox = false) -> bool
        {
            window.Pump();
            const auto draw = [&]() -> bool
            {
                if (!uploadOnce())
                    return false;
                // Two indexed submesh draws compose one rectangle; the same
                // PSO is reused with different DrawIndexedInstanced offsets.
                for (unsigned indexStart = 0; indexStart < 6; indexStart += 3)
                {
                    if (!pass.Draw(mesh, indexStart, 3, combiner, state,
                                   identity, texture0, texture1))
                    {
                        std::cerr << "W3D indexed draw: " << pass.LastError() << "\n";
                        return false;
                    }
                }
                return true;
            };
            return PresentAndVerify(device, clear, draw, &expected, expectPillarbox);
        };

        if (!runMaterial(singleStage, normal, singlePixel))
            return false;
        // Verify resident default-heap geometry is usable after its upload
        // buffers have been freed, including subsequent resize and draws.
        mesh.Release_Upload_Buffers();
        if (!runMaterial(twoStage, normal, doublePixel) ||
            !runMaterial(untextured, normal, plainPixel) ||
            !runMaterial(singleStage, rejected, clear))
            return false;

        // A near green quad must occlude a far blue quad in both draw orders.
        const std::array<float, 4> nearPixel = {{0.f, 1.f, 0.f, 1.f}};
        const auto runDepth = [&](bool nearFirst) -> bool
        {
            window.Pump();
            const auto draw = [&]() -> bool
            {
                if (!depthUploaded)
                {
                    if (!depthMesh.Upload(device, depthVertices, 8, depthIndices, 12))
                    {
                        std::cerr << "Depth geometry upload: " << depthMesh.LastError() << "\n";
                        return false;
                    }
                    depthUploaded = true;
                }
                for (unsigned i = 0; i < 2; ++i)
                {
                    const unsigned first = (nearFirst ? i : 1 - i) * 6;
                    if (!pass.Draw(depthMesh, first, 6, untextured, normal,
                                   identity, texture0, texture1))
                    {
                        std::cerr << "Depth indexed draw: " << pass.LastError() << "\n";
                        return false;
                    }
                }
                return true;
            };
            return PresentAndVerify(device, clear, draw, &nearPixel);
        };
        if (!runDepth(true) || !runDepth(false))
            return false;
        depthMesh.Release_Upload_Buffers();

        RenderBlendState translucent;
        translucent.blendEnabled = true;
        translucent.source = RENDER_BLEND_SRC_ALPHA;
        translucent.destination = RENDER_BLEND_INV_SRC_ALPHA;
        const std::array<float, 4> blendedPixel = {{0.5f, 0.f, 0.5f, 1.f}};
        const auto runBlend = [&]() -> bool
        {
            window.Pump();
            const auto draw = [&]() -> bool
            {
                if (!translucentUploaded)
                {
                    if (!translucentMesh.Upload(device, translucentVertices, 4, indices, 6))
                    {
                        std::cerr << "Water blend geometry: "
                                  << translucentMesh.LastError() << "\n";
                        return false;
                    }
                    translucentUploaded = true;
                }
                if (!pass.Draw(depthMesh, 6, 6, untextured, normal,
                               identity, texture0, texture1) ||
                    !pass.Draw(translucentMesh, 0, 6, untextured, translucent,
                               identity, texture0, texture1))
                {
                    std::cerr << "Water blend draw: " << pass.LastError() << "\n";
                    return false;
                }
                return true;
            };
            return PresentAndVerify(device, clear, draw, &blendedPixel);
        };
        if (!runBlend())
            return false;
        translucentMesh.Release_Upload_Buffers();

        // The same default-heap geometry and textures survive ResizeBuffers.
        if (!device.Resize(160, 90) || !runMaterial(singleStage, normal, singlePixel) ||
            !runDepth(true) || !runDepth(false) || !runBlend())
        {
            std::cerr << "Indexed mesh after resize: " << device.LastError() << "\n";
            return false;
        }
        // Resize to a 16:9 back buffer while retaining a square game-camera
        // projection. The center is still textured; side bars remain clear.
        device.SetSourceViewport(100, 100);
        if (!runMaterial(singleStage, normal, singlePixel, true))
        {
            std::cerr << "DX12 aspect preservation after resize failed\n";
            return false;
        }
        device.SetSourceViewport(0, 0);
        if (pass.PipelineCount() != 4)
        {
            std::cerr << "Unexpected material PSO count: " << pass.PipelineCount() << "\n";
            return false;
        }
        if (!device.WaitForIdle())
            return false;
        mesh.Reset();
        depthMesh.Reset();
        translucentMesh.Reset();
        pass.Shutdown();
        std::cout << "DX12 indexed W3D geometry, textures, depth, alpha test, blending and resize: PASS\n";
        return true;
    }
}

int wmain(int argc, wchar_t ** argv)
{
    const bool meshWarp = argc == 3 && std::wcscmp(argv[1], L"--mesh-warp") == 0;
    const bool meshHardware = argc == 3 && std::wcscmp(argv[1], L"--mesh-hardware") == 0;
    const bool warp = (argc == 2 && std::wcscmp(argv[1], L"--warp") == 0) || meshWarp;
    const bool hardware = (argc == 2 && std::wcscmp(argv[1], L"--hardware") == 0) || meshHardware;
    if (!warp && !hardware)
    {
        std::cerr << "Usage: W3DDX12PresentationSmoke --warp|--hardware"
                     "|--mesh-warp shader.hlsl|--mesh-hardware shader.hlsl\n";
        return 2;
    }

    TestWindow window;
    if (!window.Create())
    {
        std::cerr << "Unable to create Win32 presentation window\n";
        return 1;
    }

    DX12PresentationDevice device;
    if (!device.Initialize(window.window, 96, 96, warp))
    {
        std::cerr << "DX12 initialization: " << device.LastError() << "\n";
        return 1;
    }

    // Multiple presents exercise both command allocators and frame fences.
    const std::array<std::array<float, 4>, 3> colors = {{
        {{0.25f, 0.50f, 0.75f, 1.0f}},
        {{0.75f, 0.25f, 0.50f, 1.0f}},
        {{0.10f, 0.80f, 0.30f, 1.0f}}
    }};
    for (const auto & color : colors)
    {
        window.Pump();
        if (!PresentAndVerify(device, color))
            return 1;
    }
    // B04: verify a second clear issued *inside* an open frame reaches the GPU.
    // Also reject calls without a BeginFrame instead of silently succeeding.
    const std::array<float, 4> initial = {{0.f, 0.f, 0.f, 1.f}};
    const std::array<float, 4> replacement = {{0.6f, 0.2f, 0.4f, 1.f}};
    if (device.ClearActiveFrame(true, true, replacement.data(), 1.f))
    {
        std::cerr << "ClearActiveFrame accepted a call outside a frame\n";
        return 1;
    }
    if (!PresentAndVerify(device, initial,
                          [&]() { return device.ClearActiveFrame(
                              true, true, replacement.data(), 0.5f); },
                          &replacement))
        return 1;

    // A minimized window produces zero-sized resize notifications; preserve
    // buffers until a positive client-area size becomes available.
    if (!device.Resize(0, 0) || !device.IsReady())
        return 1;

    if (!device.Resize(128, 72) || device.Width() != 128 || device.Height() != 72)
    {
        std::cerr << "DX12 resize: " << device.LastError() << "\n";
        return 1;
    }
    for (const auto & color : colors)
    {
        window.Pump();
        if (!PresentAndVerify(device, color))
            return 1;
    }

    if ((meshWarp || meshHardware) && !TestIndexedMesh(device, window, argv[2]))
        return 1;
    device.Shutdown();
    if (device.IsReady())
        return 1;
    device.Shutdown(); // Shutdown is intentionally idempotent.
    std::cout << "DX12 WARP swapchain, resize, fences, pixel readback and Present: PASS\n";
    return 0;
}
