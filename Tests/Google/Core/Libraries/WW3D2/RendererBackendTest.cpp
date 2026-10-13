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

// Backend-neutral contracts must compile without including d3d8.h.
// These tests do not require a window or GPU; in-game validation separately
// verifies the DX8 implementation and real device-loss behavior.
#include "WW3D2/Renderer.h"

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
