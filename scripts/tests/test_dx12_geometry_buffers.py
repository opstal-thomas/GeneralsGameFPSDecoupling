#!/usr/bin/env python3
"""R01: contract checks for real native W3D static/dynamic geometry buffers."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WW3D = ROOT / "Core/Libraries/Source/WWVegas/WW3D2"
DX12 = WW3D / "Backend/DX12"
PRESENTATION = DX12 / "Presentation"


class R01GeometryBuffers(unittest.TestCase):
    def test_backend_implements_actual_neutral_geometry_handles(self):
        code = (DX12 / "DX12Backend.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "WW3D2/Backend/DX12/DX12W3DGeometryBuffers.h"', code)
        for method, native in (
            ("Create_Vertex_Buffer", "CreateVertex"),
            ("Create_Index_Buffer", "CreateIndex"),
            ("Create_Point_Sprite_Vertex_Buffer", "Create_Vertex_Buffer"),
            ("Release_Vertex_Buffer", "ReleaseVertex"),
            ("Release_Index_Buffer", "ReleaseIndex"),
            ("Map_Vertex_Buffer", "MapVertex"),
            ("Map_Index_Buffer", "MapIndex"),
            ("Unmap_Vertex_Buffer", "UnmapVertex"),
            ("Unmap_Index_Buffer", "UnmapIndex"),
            ("Bind_Vertex_Buffer", "BindVertex"),
            ("Bind_Index_Buffer", "BindIndex"),
            ("Bind_Dynamic_Vertex_Buffer", "BindVertex"),
            ("Bind_Dynamic_Index_Buffer", "BindIndex"),
        ):
            match = re.search(
                rf"Renderer::{method}\([^{{]*\)\s*\{{(.*?)\}}", code, re.S
            )
            self.assertIsNotNone(match, method)
            self.assertIn(native, match.group(1), method)
            self.assertNotIn("ReportPending(", match.group(1), method)
        self.assertNotIn("Direct3DCreate8", code)
        self.assertNotRegex(code, r"(?m)^\s*DX8Wrapper::")

    def test_geometry_allocations_and_fenced_lifetimes(self):
        header = (DX12 / "DX12W3DGeometryBuffers.h").read_text(encoding="utf-8")
        impl = (DX12 / "DX12W3DGeometryBuffers.cpp").read_text(encoding="utf-8")
        presentation = (DX12 / "DX12PresentationDevice.cpp").read_text(encoding="utf-8")
        for token in (
            "RenderVertexBufferHandle CreateVertex",
            "RenderIndexBufferHandle CreateIndex",
            "DX12PresentationDevice & device",
            "D3D12_HEAP_TYPE_DEFAULT",
            "D3D12_HEAP_TYPE_UPLOAD",
            "D3D12_RESOURCE_STATE_COPY_DEST",
            "D3D12_RESOURCE_STATE_INDEX_BUFFER",
            "D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER",
            "D3D12_RESOURCE_STATE_GENERIC_READ",
            "CopyBufferRegion(",
            "IASetVertexBuffers(",
            "IASetIndexBuffer(",
            "DXGI_FORMAT_R16_UINT",
            "RENDER_MAP_DISCARD",
            "RENDER_MAP_NO_OVERWRITE",
            "RENDER_MAP_READ_ONLY",
            "RetainFrameResource(",
        ):
            self.assertIn(token, impl, token)
        self.assertIn("RendererVertexBufferResource", impl)
        self.assertIn("RendererIndexBufferResource", impl)
        self.assertIn("ID3D12Resource * NativeVertexResource(", header)
        self.assertIn("GetGPUVirtualAddress()", impl)
        self.assertIn("retiredResources", presentation)
        self.assertIn("frameFences[state.currentBuffer] = signalValue", presentation)
        self.assertIn("state.CollectRetired()", presentation)
        self.assertIn("state.retiredResources.emplace_back(", presentation)
        self.assertIn("SetBoundBaseVertexIndex(", presentation)

    def test_r02_pooled_dynamic_uploads_and_fence_recycling(self):
        geometry = (DX12 / "DX12W3DGeometryBuffers.cpp").read_text(encoding="utf-8")
        presentation = (DX12 / "DX12PresentationDevice.cpp").read_text(encoding="utf-8")
        header = (DX12 / "DX12PresentationDevice.h").read_text(encoding="utf-8")
        for token in ("struct UploadPage", "ReserveUpload(", "UploadPageBytes",
                      "page.capacity - page.cursor", "page.retireFence > completed",
                      "page.owner.Get() != device.NativeDevice()",
                      "nativeOffset + offset", "RetainFrameResource(native.Get())",
                      "RENDER_MAP_DISCARD", "RENDER_MAP_NO_OVERWRITE"):
            self.assertIn(token, geometry)
        for token in ("CompletedFenceValue()", "LastSubmittedFenceValue()"):
            self.assertIn(token, geometry)
            self.assertIn(token, presentation)
            self.assertIn(token, header)
        self.assertIn("GetCompletedValue()", presentation)
        self.assertIn("const D3D12_RANGE written = {nativeOffset,", geometry)
        # Mapping the UPLOAD page itself is required; each generation writes to
        # its own offset, rather than requiring a distinct resource per update.
        self.assertIn("newNative->Map(0, &noRead, &mappedBytes)", geometry)
        self.assertIn("std::memcpy(static_cast<unsigned char *>(mappedBytes) + nativeOffset,", geometry)
        smoke = (PRESENTATION / "DX12GeometryBufferSmoke.cpp").read_text(encoding="utf-8")
        for token in ("NativeVertexOffset(", "NativeIndexOffset(",
                      "firstVertexReadback", "secondVertexReadback", "dynamicIndexReadback",
                      "First pooled dynamic snapshot survives NO_OVERWRITE on WARP",
                      "Updated pooled dynamic slice contains the new bytes on WARP",
                      "Recycle the pooled upload page only after the submission fence"):
            self.assertIn(token, smoke)

    def test_old_w3d_buffer_classes_use_selected_renderer(self):
        for filename in ("dx8vertexbuffer.cpp", "dx8indexbuffer.cpp"):
            source = (WW3D / filename).read_text(encoding="utf-8")
            self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", source)
            self.assertIn("W3D_GEOMETRY_THREAD_ASSERT()", source)
            self.assertIn("W3D_GEOMETRY_DEVICE_ASSERT()", source)
            self.assertIn("Renderer::Has_Active_Device()", source)
            self.assertIn("Renderer::Get_Render_Thread_ID()", source)
            self.assertIn("Renderer::Map_", source)
            self.assertIn("Renderer::Unmap_", source)

    def test_ci_warp_real_gpu_readback(self):
        cmake = (PRESENTATION / "CMakeLists.txt").read_text(encoding="utf-8")
        game = (WW3D / "CMakeLists.txt").read_text(encoding="utf-8")
        smoke = (PRESENTATION / "DX12GeometryBufferSmoke.cpp").read_text(encoding="utf-8")
        backend_test = (PRESENTATION / "DX12RendererFrameSmoke.cpp").read_text(
            encoding="utf-8"
        )
        for target in (cmake, game):
            self.assertIn("DX12W3DGeometryBuffers.cpp", target)
        self.assertIn("W3D_DX12_WARP_Geometry_Buffer_Residency", cmake)
        for token in (
            "MakeReadback(",
            "VerifyBytes(",
            "D3D12_HEAP_TYPE_DEFAULT",
            "D3D12_HEAP_TYPE_UPLOAD",
            "BoundBaseVertexIndex()",
            "MapVertex(",
            "MapIndex(",
            "RENDER_MAP_DISCARD",
            "RENDER_MAP_NO_OVERWRITE",
            "CopyBufferRegion(",
            "InFlightResourceCount()",
            "device.WaitForIdle()",
            "ReleaseVertex(vertex)",
            "ReleaseIndex(index)",
        ):
            self.assertIn(token, smoke, token)
        for method in (
            "Renderer::Create_Vertex_Buffer(",
            "Renderer::Create_Index_Buffer(",
            "Renderer::Map_Vertex_Buffer(",
            "Renderer::Map_Index_Buffer(",
            "Renderer::Bind_Vertex_Buffer(",
            "Renderer::Bind_Index_Buffer(",
            "Renderer::Release_Vertex_Buffer(",
            "Renderer::Release_Index_Buffer(",
        ):
            self.assertIn(method, backend_test)
        workflow = (
            ROOT / ".github/workflows/dx12-shader-validation.yml"
        ).read_text(encoding="utf-8")
        self.assertIn('foreach ($architecture in @("Win32", "x64"))', workflow)
        self.assertIn("ctest --test-dir $build -C Release --output-on-failure", workflow)


if __name__ == "__main__":
    unittest.main()
