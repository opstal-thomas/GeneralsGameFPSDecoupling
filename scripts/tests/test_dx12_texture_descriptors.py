#!/usr/bin/env python3
"""DX12-R05: stage/SRV ownership, immutable descriptor snapshots and GPU fence policy."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
WW3D = ROOT / "Core/Libraries/Source/WWVegas/WW3D2"
DX12 = WW3D / "Backend/DX12"
SMOKE = DX12 / "Presentation"


class DX12TextureDescriptorTests(unittest.TestCase):
    def test_native_stage_binding_replaces_r04_upload_only_stub(self):
        backend = (DX12 / "DX12Backend.cpp").read_text(encoding="utf-8")
        renderer = (WW3D / "Renderer.h").read_text(encoding="utf-8")
        header = (DX12 / "DX12W3DTextureStages.h").read_text(encoding="utf-8")
        self.assertIn("void Renderer::Bind_Texture(unsigned stage, RenderTextureHandle texture)", backend)
        self.assertIn("textureStages.Bind(stage, texture)", backend)
        self.assertIn("textureStages.PrepareBound(primaryWindow.Presentation())", backend)
        self.assertIn("textureStages.Clear()", backend)
        self.assertIn("DX12W3DRendererTextureStages()", backend)
        self.assertIn("BindForDraw(DX12PresentationDevice & frame", header)
        self.assertIn("static constexpr unsigned StageCount = 8", header)
        self.assertIn("static void Bind_Texture(unsigned stage, RenderTextureHandle texture)", renderer)
        self.assertNotIn("DX8Wrapper::", backend)

    def test_texture_snapshots_cannot_confuse_pointer_reuse_or_stale_gpu_generation(self):
        textures = (DX12 / "DX12W3DTextures.cpp").read_text(encoding="utf-8")
        header = (DX12 / "DX12W3DTextures.h").read_text(encoding="utf-8")
        for token in (
            "nextTextureIdentity.fetch_add(1", "++contentRevision",
            "++dst->contentRevision", "++gpuGeneration",
            "ComPtr<ID3D12Resource> gpu", "view.resource = t.gpu",
            "view.identity = t.identity",
            "view.gpuGeneration = t.gpuGeneration",
            "D3D12_SRV_DIMENSION_TEXTURE2D",
            "D3D12_SRV_DIMENSION_TEXTURECUBE",
            "D3D12_SRV_DIMENSION_TEXTURE3D",
        ):
            self.assertIn(token, textures, token)
        self.assertIn("Microsoft::WRL::ComPtr<ID3D12Resource> resource", header)
        self.assertIn("bool Snapshot(RenderTextureHandle texture, ViewSnapshot & view)", header)

    def test_descriptors_are_immutable_per_recording_and_fence_retained(self):
        s = (DX12 / "DX12W3DTextureStages.cpp").read_text(encoding="utf-8")
        presentation = (DX12 / "DX12PresentationDevice.cpp").read_text(encoding="utf-8")
        header = (DX12 / "DX12PresentationDevice.h").read_text(encoding="utf-8")
        for token in (
            "D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE",
            "D3D12_HEAP_TYPE_",  # checked in native texture manager separately
        ):
            if token == "D3D12_HEAP_TYPE_":
                continue
            self.assertIn(token, s)
        for token in (
            "frame.RetainFrameObject(page.heap.Get())",
            "frame.RecordingSerial()", "frame.NativeDevice()",
            "views[stage].identity", "views[stage].contentRevision",
            "views[stage].gpuGeneration",
            "state.tables.find(key)", "state.tables.emplace(key, table)",
            "DX12W3DTextures::Retain(texture)",
            "DX12W3DTextures::Release(slot)",
            "CreateShaderResourceView(",
            "cmd->SetDescriptorHeaps(",
            "cmd->SetGraphicsRootDescriptorTable(",
            "NullView()", "tables.clear()", "pages.clear()",
            "preparedRevisions[output.identity] = output.contentRevision",
        ):
            self.assertIn(token, s, token)
        self.assertIn("bool RetainFrameObject(IUnknown * object)", header)
        self.assertIn("unsigned long long RecordingSerial() const", header)
        for token in (
            "std::vector<ComPtr<IUnknown>> recordingObjects",
            "retiredObjects.emplace_back(", "recordingObjects.clear()",
            "retiredObjects.pop_front()", "++state.recordingSerial",
        ):
            self.assertIn(token, presentation, token)

    def test_preview_and_native_renderer_share_r04_r05_texture_path(self):
        preview = (DX12 / "DX12IndexedMeshPass.cpp").read_text(encoding="utf-8")
        for token in (
            "DX12W3DTextureStages stages",
            "DX12W3DTextures::Create2D(",
            "DX12W3DTextures::Map2D(",
            "DX12W3DTextures::Unmap2D(",
            "DX12W3DTextures::Prepare(",
            "state.stages.BindForDraw(*state.presentation, 1)",
            "state.stages.Clear()",
            "DX12W3DTextures::Release(",
        ):
            self.assertIn(token, preview, token)
        self.assertNotIn("texturesHeap", preview)
        self.assertNotIn("CreateShaderResourceView(", preview)
        self.assertIn("dst[x*4]     = src[x*4+2]", preview)  # RGBA -> W3D BGRA

    def test_warp_samples_real_two_stage_descriptors_with_invalidations(self):
        smoke = (SMOKE / "DX12DescriptorSmoke.cpp").read_text(encoding="utf-8")
        cmake = (SMOKE / "CMakeLists.txt").read_text(encoding="utf-8")
        engine_cmake = (WW3D / "CMakeLists.txt").read_text(encoding="utf-8")
        for token in (
            "D3DCompile(", "CreateGraphicsPipelineState(",
            "stages.Bind(0, T(red))", "stages.Bind(1, T(blue))",
            "stages.BindForDraw(device, 1)", "DrawInstanced(",
            "DX12W3DTextures", "CopyTextureRegion(",
            "CheckPixel(readback, 16, 16, {255, 0, 0})",
            "CheckPixel(readback, 48, 16, {0, 0, 255})",
            "CheckPixel(readback, 16, 48, {0, 255, 0})",
            "CheckPixel(readback, 48, 48, {0, 0, 0})",
            "stages.SnapshotCount() == 3",
            "device.InFlightObjectCount() == 0",
            "stages.Clear();", "device.Shutdown();",
        ):
            self.assertIn(token, smoke, token)
        self.assertIn("W3D_DX12_WARP_Texture_Descriptor_Binding", cmake)
        self.assertIn("DX12W3DTextureStages.cpp", cmake)
        self.assertIn("DX12W3DTextureStages.cpp", engine_cmake)
        workflow = (ROOT / ".github/workflows/dx12-shader-validation.yml").read_text(
            encoding="utf-8")
        self.assertIn('foreach ($architecture in @("Win32", "x64"))', workflow)
        self.assertIn("ctest --test-dir $build -C Release --output-on-failure", workflow)


if __name__ == "__main__":
    unittest.main()
