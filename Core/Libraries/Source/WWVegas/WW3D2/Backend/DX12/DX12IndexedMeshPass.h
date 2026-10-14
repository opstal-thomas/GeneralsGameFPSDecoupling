/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License, version 3 or later.
*/

// Experimental W3D-style indexed geometry submission on Direct3D 12.
// Backend-private: gameplay must continue using WW3D2/Renderer.h.
#pragma once

#include "../../Renderer.h"

#include <cstdint>
#include <memory>

class DX12PresentationDevice;

// Matches the input semantics of W3DLegacyUnlit.hlsl. This is a deliberate
// subset, not a replacement for the legacy W3D mesh vertex formats.
struct DX12W3DVertex
{
    float position[3];
    float diffuse[4];
    float uv0[2];
    float uv1[2];
};

// GPU-resident 16-bit indexed geometry. Upload must be recorded during an
// active DX12PresentationDevice frame. Retain this object until all submitted
// frames that use it have finished; synchronize before Reset/destruction.
class DX12W3DIndexedMesh final
{
public:
    DX12W3DIndexedMesh();
    ~DX12W3DIndexedMesh();

    DX12W3DIndexedMesh(const DX12W3DIndexedMesh &) = delete;
    DX12W3DIndexedMesh & operator=(const DX12W3DIndexedMesh &) = delete;

    bool Upload(DX12PresentationDevice & device,
                const DX12W3DVertex * vertices, unsigned vertexCount,
                const std::uint16_t * indices, unsigned indexCount);
    void Reset();
    bool IsReady() const;
    unsigned IndexCount() const;
    const char * LastError() const;

private:
    friend class DX12W3DIndexedMeshPass;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// W3D material permutations become specialized SM5.1 pixel shaders/PSOs.
// Only opaque/alpha-tested, unlit position/diffuse/two-UV mesh rendering is
// supported. Blending, depth, fog, lighting, complex FVF and bump mapping
// are explicitly rejected until they have backend-equivalent implementations.
class DX12W3DIndexedMeshPass final
{
public:
    DX12W3DIndexedMeshPass();
    ~DX12W3DIndexedMeshPass();

    DX12W3DIndexedMeshPass(const DX12W3DIndexedMeshPass &) = delete;
    DX12W3DIndexedMeshPass & operator=(const DX12W3DIndexedMeshPass &) = delete;

    // shaderSource is used only by this experimental/dev build. Production
    // DX12 must embed shader bytecode rather than require an external HLSL.
    bool Initialize(DX12PresentationDevice & device, const wchar_t * shaderSource);
    void Shutdown();

    // Creates a RGBA8 SRV on the selected device and records a texture upload
    // into the active frame. The pass retains staging resources until shutdown.
    bool UploadTextureRGBA8(const std::uint8_t * pixels, unsigned width,
                            unsigned height, unsigned & textureId);

    // Draws an indexed range from a resident mesh. Both stage textures must
    // be valid handles even if the shader permutation disables texturing.
    // matrix is a row-major float4x4 in W3D's existing shader convention.
    bool Draw(const DX12W3DIndexedMesh & mesh,
              unsigned firstIndex, unsigned indexCount,
              const RenderTextureCombinerKey & combiner,
              const RenderBlendState & blend,
              const float worldViewProjection[16],
              unsigned texture0, unsigned texture1);

    unsigned PipelineCount() const;
    const char * LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
