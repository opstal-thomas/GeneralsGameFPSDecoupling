#!/usr/bin/env python3
"""DX12-R07: enforce real native W3D render target/surface and MSAA contracts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
WW3D = ROOT / "Core/Libraries/Source/WWVegas/WW3D2"
DX12 = WW3D / "Backend/DX12"
WARP = DX12 / "Presentation"


class DX12R07RenderTargetTests(unittest.TestCase):
    def test_selected_renderer_exposes_native_target_and_surface_calls(self):
        code = (DX12 / "DX12Backend.cpp").read_text(encoding="utf-8")
        for signature, delegated in (
            ("Create_Offscreen_Texture(", "renderTargets.CreateOffscreenTexture("),
            ("Acquire_Current_Color_Surface(", "renderTargets.AcquireCurrentColor("),
            ("Acquire_Back_Buffer_Surface(", "renderTargets.AcquirePrimaryColor("),
            ("Acquire_Current_Depth_Surface(", "renderTargets.AcquireCurrentDepth("),
            ("Acquire_Texture_Surface_Level(", "renderTargets.AcquireTextureLevel("),
            ("Create_Depth_Texture_2D(", "renderTargets.CreateDepthTexture("),
            ("Set_Render_Target_Surfaces(", "renderTargets.Bind("),
        ):
            self.assertIn("Renderer::" + signature, code)
            self.assertIn(delegated, code)
        self.assertIn("renderTargets.BeginFrame(primaryWindow.Presentation())", code)
        self.assertIn("renderTargets.EndFrame(primaryWindow.Presentation())", code)
        self.assertIn("renderTargets.Clear(", code)
        self.assertIn("if (primaryWindow.IsSceneOpen())", code)
        self.assertIn("currently bound RTV/DSV views", code)
        self.assertIn("renderTargets.Owns(surface)", code)
        self.assertIn("renderTargets.Release(surface)", code)
        self.assertIn("primaryWindow.RestorePrimaryViewport()", code)
        self.assertIn("textureStages.Bound(stage)", code)
        self.assertIn("renderTargets.BoundDepthTexture()", code)
        self.assertIn("texture == renderTargets.BoundColorTexture()", code)
        self.assertIn("texture == renderTargets.BoundDepthTexture()", code)
        self.assertNotIn("DX8Wrapper::", code)

    def test_attachment_texture_has_real_native_flags_and_srv_generations(self):
        texture = (DX12 / "DX12W3DTextures.cpp").read_text(encoding="utf-8")
        header = (DX12 / "DX12W3DTextures.h").read_text(encoding="utf-8")
        for token in (
            "D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET",
            "D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL",
            "DXGI_FORMAT_R32_TYPELESS",
            "DXGI_FORMAT_D32_FLOAT",
            "DXGI_FORMAT_R24G8_TYPELESS",
            "DXGI_FORMAT_D24_UNORM_S8_UINT",
            "DXGI_FORMAT_R16_TYPELESS",
            "DXGI_FORMAT_D16_UNORM",
            "DXGI_FORMAT_R24_UNORM_X8_TYPELESS",
            "D3D12_RESOURCE_STATE_RESOLVE_DEST",
            "EnsureAttachmentNative",
            "++gpuGeneration",
            "++contentRevision",
            "frame.RegisterFrameResource",
            "frame.RetainFrameResource",
            "frame.TransitionFrameResource",
            "view.descriptor.Format = t.depthAttachment",
        ):
            self.assertIn(token, texture, token)
        self.assertIn("bool IsDepthAttachment(", header)
        self.assertIn("bool TransitionAttachment(", header)
        self.assertIn("RenderTexture2DHandle CreateDepth2D(", header)
        self.assertIn("R07 GPU render targets cannot be CPU-mapped", texture)
        self.assertIn("GPU attachment copies require R08", texture)

    def test_offscreen_target_manager_uses_same_b05_command_list_and_fence(self):
        source = (DX12 / "DX12W3DRenderTargets.cpp").read_text(encoding="utf-8")
        header = (DX12 / "DX12W3DRenderTargets.h").read_text(encoding="utf-8")
        for token in (
            "D3D12_DESCRIPTOR_HEAP_TYPE_RTV",
            "D3D12_DESCRIPTOR_HEAP_TYPE_DSV",
            "D3D12_RTV_DIMENSION_TEXTURE2DMS",
            "D3D12_DSV_DIMENSION_TEXTURE2D",
            "D3D12_RESOURCE_STATE_RENDER_TARGET",
            "D3D12_RESOURCE_STATE_DEPTH_WRITE",
            "D3D12_RESOURCE_STATE_RESOLVE_SOURCE",
            "D3D12_RESOURCE_STATE_RESOLVE_DEST",
            "D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS",
            "CheckFeatureSupport(",
            "ResolveSubresource(",
            "frame.RetainFrameObject(",
            "frame.RetainFrameResource(",
            "frame.TransitionFrameResource(",
            "DX12W3DTextures::TransitionAttachment(",
            "OMSetRenderTargets(",
            "RSSetViewports(",
            "RSSetScissorRects(",
            "ValidSurface(c, frame)",
            "frame.RecordingSerial()",
            "surface->refs.fetch_sub(",
        ):
            self.assertIn(token, source, token)
        self.assertIn("RenderSurfaceHandle AcquireTextureLevel(", header)
        self.assertIn("RenderSurfaceHandle CreateMSAAColor(", header)
        self.assertIn("RenderTextureHandle BoundDepthTexture() const", header)
        self.assertIn("hasColor ? 1u : 0u", source)
        self.assertIn("impl_->activeDepth ? nullptr : frame.CurrentBackBuffer()", source)
        self.assertIn("Cannot clear a color target in a depth-only pass", source)
        self.assertIn("RenderTexture2DHandle ResolvedTexture(", header)
        self.assertIn("bool Bind(", header)
        self.assertIn("bool EndFrame(", header)
        self.assertNotIn("Direct3DCreate8", source)
        self.assertNotIn("DX8Wrapper::", source)

    def test_both_warp_architectures_read_pixels_and_reject_invalid_targets(self):
        cmake = (WARP / "CMakeLists.txt").read_text(encoding="utf-8")
        engine = (WW3D / "CMakeLists.txt").read_text(encoding="utf-8")
        smoke = (WARP / "DX12RenderTargetSmoke.cpp").read_text(encoding="utf-8")
        for source in (cmake, engine):
            self.assertIn("DX12W3DRenderTargets.cpp", source)
        self.assertIn("W3D_DX12_WARP_Offscreen_Render_Targets", cmake)
        for token in (
            "D24S8",
            "CreateMSAAColor(",
            "ResolveSubresource",
            "CopyTextureRegion(",
            "Pixel(offscreenPixels",
            "Pixel(multisamplePixels",
            "Reject offscreen RTV/DSV dimensions mismatch",
            "Reject MSAA 4x color with single-sample DSV",
            "Reject depth clear while primary DSV is unbound",
            "Bind depth-only W3D shadow pass without an RTV",
            "Reject color clear when no RTV is bound",
            "Record depth/stencil clear with zero active RTVs",
            "Reject stale primary surface handle after resize",
            "Rehydrate offscreen texture/RTV after device reset",
            "frame.InFlightResourceCount() == 0",
            "frame.InFlightObjectCount() == 0",
        ):
            # ResolveSubresource is in the manager, not duplicated in smoke.
            self.assertIn(token, smoke if token != "ResolveSubresource" else
                          (DX12 / "DX12W3DRenderTargets.cpp").read_text(encoding="utf-8"))
        renderer_smoke = (WARP / "DX12RendererFrameSmoke.cpp").read_text(encoding="utf-8")
        self.assertIn("Renderer supports a depth-only shadow-map pass", renderer_smoke)
        self.assertIn("prevents binding currently writable depth as an SRV", renderer_smoke)
        workflow = (ROOT / ".github/workflows/dx12-shader-validation.yml").read_text(encoding="utf-8")
        self.assertIn('foreach ($architecture in @("Win32", "x64"))', workflow)
        self.assertIn("ctest --test-dir $build -C Release --output-on-failure", workflow)


if __name__ == "__main__":
    unittest.main()
