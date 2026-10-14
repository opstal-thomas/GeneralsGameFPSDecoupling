/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 Contributors
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/
#include <gtest/gtest.h>
#include <type_traits>

// Backend-neutral contracts must compile without including d3d8.h.
// These tests do not require a window or GPU; in-game validation separately
// verifies the DX8 implementation and real device-loss behavior.
#include "WW3D2/Renderer.h"

TEST(RendererBackend, VertexMaterialDescriptionPreservesColorAndSpecularPower)
{
    const RenderVertexMaterialDescription defaults;
    EXPECT_FLOAT_EQ(1.0f, defaults.diffuse.r);
    EXPECT_FLOAT_EQ(1.0f, defaults.diffuse.a);
    EXPECT_FLOAT_EQ(1.0f, defaults.ambient.g);
    EXPECT_FLOAT_EQ(0.0f, defaults.specular.r);
    EXPECT_FLOAT_EQ(0.0f, defaults.emissive.b);
    EXPECT_FLOAT_EQ(1.0f, defaults.power);

    RenderVertexMaterialDescription material;
    material.diffuse = RenderMaterialColor(0.2f, 0.4f, 0.6f, 0.8f);
    material.specular = RenderMaterialColor(0.9f, 0.7f, 0.5f, 0.3f);
    material.power = 32.0f;
    EXPECT_FLOAT_EQ(0.8f, material.diffuse.a);
    EXPECT_FLOAT_EQ(0.7f, material.specular.g);
    EXPECT_FLOAT_EQ(32.0f, material.power);
    static_assert(std::is_same_v<decltype(&Renderer::Set_Vertex_Material),
                                 void (*)(const RenderVertexMaterialDescription &)>);
}

TEST(RendererBackend, VertexMaterialColorSourcesAreApiIndependent)
{
    RenderMaterialColorSources defaults;
    EXPECT_EQ(RENDER_MATERIAL_COLOR, defaults.ambient);
    EXPECT_EQ(RENDER_MATERIAL_COLOR, defaults.diffuse);
    EXPECT_EQ(RENDER_MATERIAL_COLOR, defaults.emissive);

    RenderMaterialColorSources lit;
    lit.ambient = RENDER_VERTEX_COLOR_1;
    lit.diffuse = RENDER_VERTEX_COLOR_2;
    EXPECT_NE(lit.ambient, lit.diffuse);
    static_assert(std::is_same_v<decltype(&Renderer::Set_Vertex_Lighting),
                                 void (*)(bool)>);
    static_assert(std::is_same_v<decltype(&Renderer::Set_Vertex_Color_Sources),
                                 void (*)(const RenderMaterialColorSources &)>);
    static_assert(std::is_same_v<decltype(&Renderer::Set_Texture_UV_Passthrough),
                                 void (*)(unsigned, unsigned)>);
}

TEST(RendererBackend, BlendStateDescribesOpaqueAndTransparentMaterials)
{
    const RenderBlendState opaque;
    EXPECT_TRUE(opaque.Is_Opaque());
    EXPECT_FALSE(opaque.blendEnabled);
    EXPECT_FALSE(opaque.alphaTestEnabled);
    EXPECT_EQ(RENDER_BLEND_ONE, opaque.source);
    EXPECT_EQ(RENDER_BLEND_ZERO, opaque.destination);
    EXPECT_EQ(0x60u, opaque.alphaReference);
    EXPECT_EQ(RENDER_COMPARE_GREATER_EQUAL, opaque.alphaCompare);
    EXPECT_TRUE(opaque.colorWriteEnabled);

    RenderBlendState alphaBlend;
    alphaBlend.source = RENDER_BLEND_SRC_ALPHA;
    alphaBlend.destination = RENDER_BLEND_INV_SRC_ALPHA;
    alphaBlend.blendEnabled = true;
    EXPECT_FALSE(alphaBlend.Is_Opaque());

    RenderBlendState masked;
    masked.alphaTestEnabled = true;
    masked.alphaReference = 0x9f;
    masked.alphaCompare = RENDER_COMPARE_LESS_EQUAL;
    EXPECT_FALSE(masked.Is_Opaque());
    EXPECT_EQ(0x9fu, masked.alphaReference);
}

TEST(RendererBackend, DepthAndCullModesAreBackendNeutral)
{
    EXPECT_NE(RENDER_COMPARE_LESS_EQUAL, RENDER_COMPARE_GREATER_EQUAL);
    EXPECT_NE(RENDER_CULL_NONE, RENDER_CULL_CW);
    EXPECT_NE(RENDER_CULL_CW, RENDER_CULL_CCW);
    static_assert(std::is_same_v<decltype(&Renderer::Apply_Blend_State),
                                 void (*)(const RenderBlendState &)>);
    static_assert(std::is_same_v<decltype(&Renderer::Set_Depth_Compare),
                                 void (*)(RenderCompareFunc)>);
    static_assert(std::is_same_v<decltype(&Renderer::Set_Cull_Mode),
                                 void (*)(RenderCullMode)>);
}

TEST(RendererBackend, SamplerDescriptionIsIndependentOfDirect3D)
{
    const RenderSamplerDescription defaults;
    EXPECT_EQ(RENDER_SAMPLER_POINT, defaults.minFilter);
    EXPECT_EQ(RENDER_SAMPLER_POINT, defaults.magFilter);
    EXPECT_EQ(RENDER_SAMPLER_NONE, defaults.mipFilter);
    EXPECT_EQ(RENDER_SAMPLER_REPEAT, defaults.addressU);
    EXPECT_EQ(RENDER_SAMPLER_REPEAT, defaults.addressV);

    RenderSamplerDescription trilinear;
    trilinear.minFilter = RENDER_SAMPLER_LINEAR;
    trilinear.magFilter = RENDER_SAMPLER_LINEAR;
    trilinear.mipFilter = RENDER_SAMPLER_LINEAR;
    trilinear.addressU = RENDER_SAMPLER_CLAMP;
    EXPECT_NE(trilinear.mipFilter, defaults.mipFilter);
    EXPECT_NE(trilinear.addressU, defaults.addressU);
    static_assert(std::is_same_v<decltype(&Renderer::Apply_Texture_Sampler),
                                 void (*)(unsigned, const RenderSamplerDescription &)>);
}

TEST(RendererBackend, SurfaceResourcesAreSeparateFromTextures)
{
    RenderSurfaceDescription empty;
    EXPECT_FALSE(empty.Is_Valid());

    RenderSurfaceDescription surface;
    surface.width = 256;
    surface.height = 128;
    surface.pixelFormat = 5;
    EXPECT_TRUE(surface.Is_Valid());
    EXPECT_EQ(5u, surface.pixelFormat);

    static_assert(std::is_pointer_v<RenderSurfaceHandle>);
    static_assert(!std::is_convertible_v<RenderSurfaceHandle, RenderTexture2DHandle>);
    static_assert(!std::is_convertible_v<RenderSurfaceHandle, RenderTextureHandle>);
    static_assert(!std::is_convertible_v<RenderSurfaceHandle, RenderVertexBufferHandle>);
    static_assert(std::is_same_v<decltype(Renderer::Create_Surface(surface)), RenderSurfaceHandle>);
    static_assert(std::is_same_v<decltype(Renderer::Load_Surface("test")), RenderSurfaceHandle>);

    Renderer::Retain_Surface(nullptr);
    Renderer::Release_Surface(nullptr);
    RenderSurfaceDescription nullDescription;
    Renderer::Describe_Surface(nullptr, nullDescription);
    EXPECT_FALSE(nullDescription.Is_Valid());
}

TEST(RendererBackend, CubeAndVolumeTextureDescriptors)
{
    const RenderTextureCubeDescription emptyCube;
    const RenderTextureVolumeDescription emptyVolume;
    EXPECT_FALSE(emptyCube.Is_Valid());
    EXPECT_FALSE(emptyVolume.Is_Valid());

    RenderTextureCubeDescription cube;
    cube.edgeLength = 512;
    cube.mipLevels = 1;
    cube.memory = RENDER_TEXTURE_DEFAULT;
    cube.renderTarget = true;
    EXPECT_TRUE(cube.Is_Valid());
    EXPECT_TRUE(cube.renderTarget);

    RenderTextureVolumeDescription volume;
    volume.width = 32;
    volume.height = 24;
    volume.depth = 8;
    volume.memory = RENDER_TEXTURE_MANAGED;
    EXPECT_TRUE(volume.Is_Valid());
    EXPECT_EQ(8u, volume.depth);
    volume.depth = 0;
    EXPECT_FALSE(volume.Is_Valid());

    static_assert(std::is_pointer_v<RenderTextureCubeHandle>);
    static_assert(std::is_pointer_v<RenderTextureVolumeHandle>);
    static_assert(!std::is_convertible_v<RenderTextureCubeHandle, RenderTextureVolumeHandle>);
    static_assert(!std::is_convertible_v<RenderTextureCubeHandle, RenderTexture2DHandle>);
    static_assert(!std::is_convertible_v<RenderTextureVolumeHandle, RenderTexture2DHandle>);
    static_assert(std::is_same_v<decltype(Renderer::Create_Texture_Cube(cube)),
                                 RenderTextureCubeHandle>);
    static_assert(std::is_same_v<decltype(Renderer::Create_Texture_Volume(volume)),
                                 RenderTextureVolumeHandle>);
}

TEST(RendererBackend, TextureBindingHandleAcceptsAllTextureTypes)
{
    // A common texture binding handle must remain distinct from both
    // render-target creation and geometry resource handles.
    static_assert(std::is_pointer_v<RenderTextureHandle>);
    static_assert(!std::is_convertible_v<RenderTexture2DHandle, RenderTextureHandle>);
    static_assert(!std::is_convertible_v<RenderTextureHandle, RenderTexture2DHandle>);
    static_assert(!std::is_convertible_v<RenderTextureHandle, RenderVertexBufferHandle>);
    static_assert(!std::is_convertible_v<RenderTextureHandle, RenderIndexBufferHandle>);
    static_assert(std::is_same_v<decltype(&Renderer::Bind_Texture),
        void (*)(unsigned, RenderTextureHandle)>);
    static_assert(std::is_same_v<decltype(&Renderer::Retain_Texture),
        void (*)(RenderTextureHandle)>);
    static_assert(std::is_same_v<decltype(&Renderer::Release_Texture),
        void (*)(RenderTextureHandle)>);

    // Reference operations with null must never require an initialized
    // graphics device. The real texture stage requires a graphics device.
    Renderer::Retain_Texture(nullptr);
    Renderer::Release_Texture(nullptr);
}

TEST(RendererBackend, TextureDescriptorsSupportRenderTargetsAndDepth)
{
    const RenderTexture2DDescription uninitialized;
    EXPECT_FALSE(uninitialized.Is_Valid());
    EXPECT_EQ(RENDER_TEXTURE_MANAGED, uninitialized.memory);
    EXPECT_FALSE(uninitialized.renderTarget);
    EXPECT_EQ(0u, uninitialized.mipLevels);

    RenderTexture2DDescription target;
    target.width = 1024;
    target.height = 1024;
    target.pixelFormat = 17;
    target.mipLevels = 1;
    target.memory = RENDER_TEXTURE_DEFAULT;
    target.renderTarget = true;
    EXPECT_TRUE(target.Is_Valid());
    EXPECT_TRUE(target.renderTarget);
    static_assert(std::is_pointer_v<RenderTexture2DHandle>);
    static_assert(!std::is_convertible_v<RenderTexture2DHandle, RenderVertexBufferHandle>);
    static_assert(std::is_same_v<decltype(Renderer::Create_Texture_2D(target)),
                                 RenderTexture2DHandle>);
}

TEST(RendererBackend, BufferHandlesAreOpaqueAndTypeSafe)
{
    static_assert(std::is_pointer_v<RenderVertexBufferHandle>);
    static_assert(std::is_pointer_v<RenderIndexBufferHandle>);
    static_assert(!std::is_convertible_v<RenderVertexBufferHandle, RenderIndexBufferHandle>);
    static_assert(!std::is_convertible_v<RenderIndexBufferHandle, RenderVertexBufferHandle>);
    static_assert(std::is_same_v<decltype(Renderer::Create_Vertex_Buffer(RenderBufferDescription{})),
                                 RenderVertexBufferHandle>);
    static_assert(std::is_same_v<decltype(Renderer::Create_Index_Buffer(RenderBufferDescription{})),
                                 RenderIndexBufferHandle>);

    RenderVertexBufferHandle vertexBuffer = nullptr;
    RenderIndexBufferHandle indexBuffer = nullptr;
    EXPECT_EQ(nullptr, vertexBuffer);
    EXPECT_EQ(nullptr, indexBuffer);
}

TEST(RendererBackend, MappingFlagsAreIndependentOfNativeD3DEnums)
{
    EXPECT_EQ(0u, static_cast<unsigned>(RENDER_MAP_NONE));
    EXPECT_NE(RENDER_MAP_DISCARD, RENDER_MAP_NO_OVERWRITE);
    EXPECT_NE(RENDER_MAP_NO_SYSTEM_LOCK, RENDER_MAP_READ_ONLY);
    const unsigned dynamicMap = RENDER_MAP_NO_SYSTEM_LOCK | RENDER_MAP_DISCARD;
    EXPECT_TRUE((dynamicMap & RENDER_MAP_DISCARD) != 0);
    EXPECT_FALSE((dynamicMap & RENDER_MAP_NO_OVERWRITE) != 0);
}

TEST(RendererBackend, ResourceDescriptionsUseBackendNeutralFlags)
{
    RenderBufferDescription vertexBuffer;
    EXPECT_FALSE(vertexBuffer.Is_Valid());
    EXPECT_FALSE(vertexBuffer.Is_Dynamic());

    vertexBuffer.byteSize = 4096;
    vertexBuffer.legacyVertexFormat = 0x112;
    vertexBuffer.usageFlags =
        RENDER_BUFFER_DYNAMIC | RENDER_BUFFER_NPATCHES;
    EXPECT_TRUE(vertexBuffer.Is_Valid());
    EXPECT_TRUE(vertexBuffer.Is_Dynamic());
    EXPECT_EQ(4096u, vertexBuffer.byteSize);
    EXPECT_EQ(0x112u, vertexBuffer.legacyVertexFormat);
    EXPECT_TRUE((vertexBuffer.usageFlags & RENDER_BUFFER_NPATCHES) != 0);

    RenderBufferDescription indices;
    indices.byteSize = 128 * sizeof(unsigned short);
    EXPECT_TRUE(indices.Is_Valid());
    EXPECT_EQ(0u, indices.legacyVertexFormat);
    EXPECT_FALSE(indices.Is_Dynamic());
}

TEST(RendererBackend, UninitializedCapabilitiesDoNotAdvertiseFeatures)
{
    const RenderCapabilities caps;
    EXPECT_FALSE(caps.hardwareTransformAndLighting);
    EXPECT_FALSE(caps.nPatches);
    EXPECT_FALSE(caps.textureCompression);
    EXPECT_FALSE(caps.anisotropicFiltering);
    EXPECT_FALSE(caps.cubeTextures);
    EXPECT_FALSE(caps.bumpEnvironmentMapping);
    EXPECT_EQ(0u, caps.maxSimultaneousTextures);
}

TEST(RendererBackend, OnlyReadyDevicesAllowResourceUpdates)
{
    EXPECT_FALSE(Renderer::Can_Render(Renderer::DEVICE_UNAVAILABLE));
    EXPECT_TRUE(Renderer::Can_Render(Renderer::DEVICE_READY));
    EXPECT_FALSE(Renderer::Can_Render(Renderer::DEVICE_LOST));
    EXPECT_FALSE(Renderer::Can_Render(Renderer::DEVICE_RESET_REQUIRED));
    EXPECT_FALSE(Renderer::Can_Render(Renderer::DEVICE_ERROR));
}

TEST(RendererBackend, OnlyResetRequiredDevicesTriggerRecovery)
{
    EXPECT_FALSE(Renderer::Requires_Reset(Renderer::DEVICE_UNAVAILABLE));
    EXPECT_FALSE(Renderer::Requires_Reset(Renderer::DEVICE_READY));
    EXPECT_FALSE(Renderer::Requires_Reset(Renderer::DEVICE_LOST));
    EXPECT_TRUE(Renderer::Requires_Reset(Renderer::DEVICE_RESET_REQUIRED));
    EXPECT_FALSE(Renderer::Requires_Reset(Renderer::DEVICE_ERROR));
}

TEST(RendererBackend, ViewportAndRasterModesAreApiNeutral)
{
    RenderViewport viewport = { 2, 3, 1280, 720, 0.0f, 1.0f };
    EXPECT_EQ(2u, viewport.x);
    EXPECT_EQ(3u, viewport.y);
    EXPECT_EQ(1280u, viewport.width);
    EXPECT_EQ(720u, viewport.height);
    EXPECT_FLOAT_EQ(0.0f, viewport.min_z);
    EXPECT_FLOAT_EQ(1.0f, viewport.max_z);

    EXPECT_NE(Renderer::POLYGON_POINT, Renderer::POLYGON_LINE);
    EXPECT_NE(Renderer::POLYGON_LINE, Renderer::POLYGON_FILL);
}
