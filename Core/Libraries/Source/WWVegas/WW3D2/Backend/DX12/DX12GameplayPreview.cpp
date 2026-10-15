/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License, version 3 or later.
*/

// A separate opt-in DX12 window receives a strictly bounded subset of the
// game's visible, rigid W3D mesh submissions. The game continues drawing and
// presenting through DX8. This is intentionally not a visually complete backend.
#include "DX12GameplayPreview.h"
#include "DX12PresentationDevice.h"
#include "DX12IndexedMeshPass.h"
#include "../../Renderer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr unsigned PreviewWidth = 640;
    constexpr unsigned PreviewHeight = 480;
    constexpr unsigned MaxMeshesPerFrame = 12;
    constexpr unsigned MaxVerticesPerMesh = 2048;
    constexpr unsigned MaxIndicesPerMesh = 4096 * 3;
    constexpr unsigned MaxCapturedTextures = 64;
    constexpr unsigned MaxTextureDimension = 512;

    bool EnvironmentEnabled(const wchar_t * name)
    {
        wchar_t value[8] = {};
        return GetEnvironmentVariableW(name, value, 8) == 1 && value[0] == L'1';
    }

    std::wstring ShaderFile()
    {
        wchar_t overridePath[MAX_PATH] = {};
        const DWORD envLength = GetEnvironmentVariableW(
            L"W3D_DX12_PREVIEW_SHADER", overridePath, MAX_PATH);
        if (envLength > 0 && envLength < MAX_PATH)
            return std::wstring(overridePath);

        wchar_t executable[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return L"";
        std::wstring path(executable, length);
        const auto lastSlash = path.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
            path.resize(lastSlash + 1);
        else
            path.clear();
        return path + L"W3DLegacyUnlit.hlsl";
    }

    void Diagnostic(const char * stage, const char * error)
    {
        std::string message = "[W3D DX12 gameplay preview] ";
        message += stage;
        if (error && *error)
        {
            message += ": ";
            message += error;
        }
        message += "\n";
        OutputDebugStringA(message.c_str());
    }
}

struct DX12GameplayPreview::Impl
{
    HWND window = nullptr;
    bool attempted = false;
    bool ready = false;
    bool recording = false;
    unsigned meshesThisFrame = 0;
    unsigned whiteTexture0 = 0;
    unsigned whiteTexture1 = 0;
    bool texturesUploaded = false;
    // Cache by the currently loaded render-resource identity. Readbacks and
    // uploads are bounded; resources stay alive through preview shutdown.
    std::unordered_map<RenderTextureHandle, unsigned> textureCache;
    DX12PresentationDevice presentation;
    DX12W3DIndexedMeshPass pass;
    // Upload heaps and GPU buffers must outlive the submitted frame. End_Frame
    // waits for idle before discarding these diagnostic-only resources.
    std::vector<std::unique_ptr<DX12W3DIndexedMesh>> meshes;

    void Report(const char * stage, const char * reason)
    {
        Diagnostic(stage, reason);
    }
};

DX12GameplayPreview::DX12GameplayPreview() : impl_(new Impl) {}
DX12GameplayPreview::~DX12GameplayPreview() { Shutdown(); }

DX12GameplayPreview & DX12GameplayPreview::Instance()
{
    static DX12GameplayPreview instance;
    return instance;
}

bool DX12GameplayPreview::Is_Recording() const
{
    return impl_->ready && impl_->recording;
}

void DX12GameplayPreview::Try_Initialize()
{
    Impl & state = *impl_;
    if (state.attempted)
        return;
    state.attempted = true;

    if (!EnvironmentEnabled(L"W3D_DX12_PREVIEW"))
        return;

    // A separate, non-activating top-level window means DX8 keeps ownership
    // of the gameplay swapchain, window messages and device-reset behavior.
    state.window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"STATIC", L"W3D experimental DX12 gameplay mesh preview",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        PreviewWidth, PreviewHeight, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!state.window)
    {
        state.Report("CreateWindowExW", "could not create secondary preview window");
        return;
    }

    const bool warp = EnvironmentEnabled(L"W3D_DX12_PREVIEW_WARP");
    if (!state.presentation.Initialize(
            state.window, PreviewWidth, PreviewHeight, warp))
    {
        state.Report("DX12 initialization", state.presentation.LastError());
        Shutdown();
        return;
    }

    const std::wstring shaderFile = ShaderFile();
    if (shaderFile.empty() ||
        !state.pass.Initialize(state.presentation, shaderFile.c_str()))
    {
        state.Report("DX12 W3D shader", state.pass.LastError());
        Shutdown();
        return;
    }

    ShowWindow(state.window, SW_SHOWNOACTIVATE);
    state.ready = true;
    Diagnostic("preview enabled (DX8 gameplay remains active)", nullptr);
}

void DX12GameplayPreview::Begin_Frame()
{
    Impl & state = *impl_;
    if (!state.ready || state.recording)
        return;

    // Only process messages belonging to the secondary window.
    MSG message = {};
    while (PeekMessageW(&message, state.window, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!IsWindow(state.window))
    {
        Shutdown();
        return;
    }

    const float clear[] = {0.025f, 0.04f, 0.075f, 1.0f};
    if (!state.presentation.BeginFrame(clear))
    {
        state.Report("BeginFrame", state.presentation.LastError());
        Shutdown();
        return;
    }
    state.recording = true;
    state.meshesThisFrame = 0;

    if (!state.texturesUploaded)
    {
        const std::uint8_t white[] = {255, 255, 255, 255};
        if (!state.pass.UploadTextureRGBA8(white, 1, 1, state.whiteTexture0) ||
            !state.pass.UploadTextureRGBA8(white, 1, 1, state.whiteTexture1))
        {
            state.Report("Upload white diagnostic textures", state.pass.LastError());
            Shutdown();
            return;
        }
        state.texturesUploaded = true;
    }
}

void DX12GameplayPreview::Submit_Mesh(const RenderGameplayPreviewVertex * vertices,
                                      unsigned vertexCount,
                                      const unsigned short * indices,
                                      unsigned indexCount,
                                      const float worldViewProjection[16],
                                      RenderTextureHandle texture,
                                      bool alphaTest)
{
    Impl & state = *impl_;
    if (!state.recording || state.meshesThisFrame >= MaxMeshesPerFrame ||
        !vertices || !indices || !worldViewProjection ||
        vertexCount < 3 || vertexCount > MaxVerticesPerMesh ||
        indexCount < 3 || indexCount > MaxIndicesPerMesh ||
        indexCount % 3 != 0)
        return;

    // Explicitly copy to the DX12 vertex representation. Do not reinterpret
    // source W3D objects, whose lifetime ends after this synchronous call.
    static_assert(sizeof(RenderGameplayPreviewVertex) == sizeof(DX12W3DVertex),
                  "Preview and DX12 mesh vertex layouts must match");
    std::vector<DX12W3DVertex> expanded(vertexCount);
    for (unsigned i = 0; i < vertexCount; ++i)
        std::memcpy(&expanded[i], &vertices[i], sizeof(DX12W3DVertex));
    std::vector<std::uint16_t> triangleIndices(indices, indices + indexCount);

    std::unique_ptr<DX12W3DIndexedMesh> mesh(new DX12W3DIndexedMesh);
    if (!mesh->Upload(state.presentation, expanded.data(), vertexCount,
                      triangleIndices.data(), indexCount))
    {
        // Unsupported/corrupt geometry is ignored rather than affecting DX8.
        state.Report("Upload W3D gameplay mesh", mesh->LastError());
        return;
    }

    unsigned colorTexture = state.whiteTexture0;
    if (texture)
    {
        auto cached = state.textureCache.find(texture);
        if (cached != state.textureCache.end())
        {
            colorTexture = cached->second;
        }
        else if (state.textureCache.size() < MaxCapturedTextures)
        {
            RenderTextureLevelDescription description;
            Renderer::Describe_Texture_Level_2D(
                reinterpret_cast<RenderTexture2DHandle>(texture), 0, description);
            if (description.width > 0 && description.height > 0 &&
                description.width <= MaxTextureDimension &&
                description.height <= MaxTextureDimension)
            {
                std::vector<std::uint8_t> pixels(
                    static_cast<size_t>(description.width) * description.height * 4);
                unsigned width = 0, height = 0;
                unsigned uploaded = 0;
                if (Renderer::Snapshot_DX12_Preview_Texture_RGBA8(
                        texture, pixels.data(), static_cast<unsigned>(pixels.size()),
                        width, height) &&
                    state.pass.UploadTextureRGBA8(pixels.data(), width, height, uploaded))
                {
                    colorTexture = uploaded;
                    state.textureCache.emplace(texture, uploaded);
                }
            }
        }
    }

    RenderTextureCombinerKey combiner;
    RenderBlendState material;
    if (colorTexture != state.whiteTexture0)
    {
        combiner.color[0] = RenderTextureStageChannel(
            RENDER_TEXOP_MODULATE, RENDER_TEXARG_TEXTURE, RENDER_TEXARG_DIFFUSE);
        combiner.alpha[0] = RenderTextureStageChannel(
            RENDER_TEXOP_MODULATE, RENDER_TEXARG_TEXTURE, RENDER_TEXARG_DIFFUSE);
        material.alphaTestEnabled = alphaTest;
    }

    if (!state.pass.Draw(*mesh, 0, indexCount, combiner, material,
                         worldViewProjection, colorTexture,
                         state.whiteTexture1))
    {
        state.Report("Draw W3D gameplay mesh", state.pass.LastError());
        // Upload commands still reference this mesh, so it must survive the
        // frame even on a failed Draw.
        state.meshes.push_back(std::move(mesh));
        return;
    }

    state.meshes.push_back(std::move(mesh));
    ++state.meshesThisFrame;
}

void DX12GameplayPreview::End_Frame()
{
    Impl & state = *impl_;
    if (!state.recording)
        return;
    state.recording = false;
    if (!state.presentation.EndFrame(0) || !state.presentation.WaitForIdle())
    {
        state.Report("Present DX12 preview", state.presentation.LastError());
        Shutdown();
        return;
    }
    // Intentionally synchronous and small: this diagnostic is not a
    // production streaming or frame-resource allocator.
    state.meshes.clear();
}

void DX12GameplayPreview::Shutdown()
{
    Impl & state = *impl_;
    if (state.ready && !state.recording)
        state.presentation.WaitForIdle();
    state.recording = false;
    state.ready = false;
    state.meshes.clear();
    state.textureCache.clear();
    state.pass.Shutdown();
    state.presentation.Shutdown();
    if (state.window && IsWindow(state.window))
        DestroyWindow(state.window);
    state.window = nullptr;
    state.texturesUploaded = false;
}
