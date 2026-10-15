/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License, version 3 or later.
*/

// Developer-only DX12 mirror of real W3D gameplay meshes. DX8 remains the
// authoritative renderer, including the actual game window and simulation.
#pragma once

#include <memory>
#include "../../Renderer.h"

class DX12GameplayPreview final
{
public:
    static DX12GameplayPreview & Instance();

    void Try_Initialize();
    void Begin_Frame();
    void Submit_Mesh(const RenderGameplayPreviewVertex * vertices, unsigned vertexCount,
                     const unsigned short * indices, unsigned indexCount,
                     const float worldViewProjection[16],
                     RenderTextureHandle texture, bool alphaTest);
    void End_Frame();
    void Shutdown();

    bool Is_Recording() const;

private:
    DX12GameplayPreview();
    ~DX12GameplayPreview();
    DX12GameplayPreview(const DX12GameplayPreview &) = delete;
    DX12GameplayPreview & operator=(const DX12GameplayPreview &) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
