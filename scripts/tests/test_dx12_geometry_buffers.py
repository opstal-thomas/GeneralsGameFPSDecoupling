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
                rf"Renderer::{method}\([^{{]*\)\s*\{{", code, re.S
            )
            self.assertIsNotNone(match, method)
            # Parse the complete C++ method body, including nested guard
            # blocks: R02 rejects CPU-only sorting bindings explicitly.
            opening = code.index("{", match.start())
            depth = 0
            closing = None
            for pos in range(opening, len(code)):
                if code[pos] == "{":
                    depth += 1
                elif code[pos] == "}":
                    depth -= 1
                    if depth == 0:
                        closing = pos
                        break
            self.assertIsNotNone(closing, method)
            body = code[opening + 1:closing]
            self.assertIn(native, body, method)
            self.assertNotIn("ReportPending(", body, method)
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
        for token in ("struct UploadPage", "ReserveTransientGeometry(",
                      "UploadPageBytes", "page.capacity - page.cursor",
                      "page.retireFence > completed", "page.recordingId",
                      "uploadPages.clear()", "CollectTransientPages()",
                      "page.retireFence = signalValue", "UploadCacheBytes",
                      "D3D12_HEAP_TYPE_UPLOAD"):
            self.assertIn(token, presentation)
        for token in ("ReserveTransientGeometry(", "TransientGeometryPageCount()",
                      "TransientGeometryResidentBytes()"):
            self.assertIn(token, header)
            self.assertIn(token, presentation)
        self.assertNotIn("std::vector<UploadPage> uploadPages;", geometry)
        for token in ("handle->state.dynamic ? 0 : offset",
                      "sourceBytes != byteCount", "shadow.data() + byteOffset",
                      "RetainFrameResource(native.Get())",
                      "RENDER_MAP_DISCARD", "RENDER_MAP_NO_OVERWRITE",
                      "ReserveTransientGeometry(", "newNative = borrowed;"):
            self.assertIn(token, geometry)
        self.assertIn("GetCompletedValue()", presentation)
        self.assertIn("const D3D12_RANGE written = {nativeOffset,", geometry)
        self.assertIn("newNative->Map(0, &noRead, &mappedBytes)", geometry)
        self.assertIn("std::memcpy(static_cast<unsigned char *>(mappedBytes) + nativeOffset,", geometry)
        for token in ("NativeVertexSliceBytes(", "NativeIndexSliceBytes(",
                      "NativeVertexSourceOffset(", "NativeIndexSourceOffset("):
            self.assertIn(token, geometry)
            self.assertIn(token, (DX12 / "DX12W3DGeometryBuffers.h").read_text(encoding="utf-8"))
        smoke = (PRESENTATION / "DX12GeometryBufferSmoke.cpp").read_text(encoding="utf-8")
        for token in ("NativeVertexOffset(", "NativeIndexOffset(",
                      "R02 dynamic vertex and index snapshots contain only requested ranges",
                      "firstVertexReadback", "secondVertexReadback", "dynamicIndexReadback",
                      "First pooled dynamic snapshot survives NO_OVERWRITE on WARP",
                      "Updated pooled dynamic slice contains the new bytes on WARP",
                      "Recycle the pooled upload page only after the submission fence",
                      "Rebind unchanged dynamic buffer after prior page reuse",
                      "Two dynamic W3D handles survive GPU page reuse across frames",
                      "Large ring overflow allocated another page",
                      "Shutdown clears device-owned transient geometry",
                      "Recreated DX12 device starts with an empty upload pool"):
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

    def test_r02_legacy_dynamic_producer_bridges(self):
        vertex = (WW3D / "dx8vertexbuffer.cpp").read_text(encoding="utf-8")
        index = (WW3D / "dx8indexbuffer.cpp").read_text(encoding="utf-8")
        backend = (DX12 / "DX12Backend.cpp").read_text(encoding="utf-8")
        sorting = (WW3D / "sortingrenderer.cpp").read_text(encoding="utf-8")
        for source, signature in (
            (vertex, "Renderer::Bind_Dynamic_Vertex_Buffer(const DynamicVBAccessClass & access)"),
            (index, "Renderer::Bind_Dynamic_Index_Buffer(const DynamicIBAccessClass & access,"),
        ):
            self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", source)
            self.assertIn(signature, source)
            self.assertIn("access.Get_Render_Binding()", source)
        for symbol in ("Renderer::Bind_Dynamic_Vertex_Buffer(const DynamicVBAccessClass & access)",
                       "Renderer::Bind_Dynamic_Index_Buffer(const DynamicIBAccessClass & access"):
            self.assertNotIn(symbol, backend)
        self.assertNotIn('ReportPending("Renderer::Bind_Dynamic_Vertex_Buffer")', backend)
        self.assertNotIn('ReportPending("Renderer::Bind_Dynamic_Index_Buffer")', backend)
        self.assertIn("Bind_Dynamic_Index_Buffer(dyn_ib_access.Get_Render_Binding()", sorting)
        self.assertIn("Bind_Dynamic_Vertex_Buffer(dyn_vb_access.Get_Render_Binding()", sorting)

    def test_r02_dynamic_line_and_mesh_producers_use_selected_renderer(self):
        # Segment ribbons, ground marks and particle-like dynamic meshes
        # formerly bypassed the backend seam through DX8Wrapper methods.
        # The DX8 adapter delegates the neutral calls unchanged, while DX12
        # now receives W3D dynamic-buffer accessors through its pooled upload.
        for filename in ("seglinerenderer.cpp", "dynamesh.cpp"):
            source = (WW3D / filename).read_text(encoding="utf-8")
            self.assertIn("Renderer::Bind_Dynamic_Vertex_Buffer(", source)
            self.assertIn("Renderer::Bind_Dynamic_Index_Buffer(", source)
            self.assertIn("Renderer::Draw_Triangles(", source)
            self.assertNotIn("DX8Wrapper::Set_Vertex_Buffer(", source)
            self.assertNotIn("DX8Wrapper::Set_Index_Buffer(", source)
            self.assertNotIn("DX8Wrapper::Draw_Triangles(", source)
        for filename in ("streakRender.cpp", "pointgr.cpp", "line3d.cpp",
                         "sortingrenderer.cpp"):
            source = (WW3D / filename).read_text(encoding="utf-8")
            self.assertIn("Renderer::Bind_Dynamic_Vertex_Buffer(", source)
        dx8 = (WW3D / "Backend/DX8Backend.cpp").read_text(encoding="utf-8")
        self.assertIn("DX8Wrapper::Draw_Triangles(startIndex, triangleCount,", dx8)

    def test_dx12_world_shutdown_releases_legacy_dynamic_ring_owners(self):
        shutdown = (WW3D / "ww3d.cpp").read_text(encoding="utf-8")
        section = shutdown[shutdown.index("WW3DErrorType WW3D::Shutdown()"):]
        for token in ("#if defined(W3D_RENDER_BACKEND_DX12)",
                      "SortingRendererClass::Deinit();",
                      "DynamicVBAccessClass::_Deinit();",
                      "DynamicIBAccessClass::_Deinit();"):
            self.assertIn(token, section)
        self.assertLess(section.index("DynamicVBAccessClass::_Deinit();"),
                        section.index("Renderer::Shutdown();"))
        self.assertLess(section.index("DynamicIBAccessClass::_Deinit();"),
                        section.index("Renderer::Shutdown();"))

    def test_r02_sorting_terrain_and_water_source_coverage(self):
        vertex = (WW3D / "dx8vertexbuffer.cpp").read_text(encoding="utf-8")
        index = (WW3D / "dx8indexbuffer.cpp").read_text(encoding="utf-8")
        vertex_header = (WW3D / "dx8vertexbuffer.h").read_text(encoding="utf-8")
        index_header = (WW3D / "dx8indexbuffer.h").read_text(encoding="utf-8")
        backend = (DX12 / "DX12Backend.cpp").read_text(encoding="utf-8")
        geometry = (DX12 / "DX12W3DGeometryBuffers.cpp").read_text(encoding="utf-8")
        geometry_header = (DX12 / "DX12W3DGeometryBuffers.h").read_text(encoding="utf-8")
        neutral = (WW3D / "Renderer.h").read_text(encoding="utf-8")
        for token in ("const void * cpuData", "legacyVertexFormat"):
            self.assertIn(token, neutral)
        for token in ("Get_Sorting_Vertex_Array() + VertexBufferOffset",
                      "binding.legacyVertexFormat = FVFInfo.Get_FVF()",
                      "Renderer::Bind_Legacy_Vertex_Buffer("):
            self.assertIn(token, vertex)
        for token in ("Get_Sorting_Index_Array() + IndexBufferOffset",
                      "Renderer::Bind_Legacy_Index_Buffer("):
            self.assertIn(token, index)
        self.assertIn("Get_Sorting_Vertex_Array()", vertex_header)
        self.assertIn("Get_Sorting_Index_Array()", index_header)
        for token in ("BindTransientVertex(", "BindTransientIndex(",
                      "UploadTransientCPU(", "RetainFrameResource(resource)",
                      "D3D12_RESOURCE_STATE_GENERIC_READ", "IASetVertexBuffers(",
                      "IASetIndexBuffer(", "SetBoundBaseVertexIndex(baseVertexIndex)"):
            self.assertIn(token, geometry)
        for token in ("BindTransientVertex(", "BindTransientIndex(",
                      "TransientGeometrySnapshot"):
            self.assertIn(token, geometry_header)
        for token in ("binding.cpuData", "binding.legacyVertexFormat",
                      "DX12W3DGeometryBuffers::BindTransientVertex(",
                      "DX12W3DGeometryBuffers::BindTransientIndex(",
                      "SetBoundBaseVertexIndex(offset)"):
            self.assertIn(token, backend)
        for op in ("Bind_Legacy_Vertex_Buffer", "Bind_Legacy_Index_Buffer",
                   "Set_Index_Buffer_Index_Offset", "Unbind_Sorting_Geometry"):
            self.assertNotIn('ReportPending("Renderer::' + op, backend)
        terrain = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTerrainTracks.cpp"
                   ).read_text(encoding="utf-8")
        water = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWaterTracks.cpp"
                 ).read_text(encoding="utf-8")
        particles = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTerrainParticles.cpp"
                     ).read_text(encoding="utf-8")
        for token in ("Renderer::Bind_Legacy_Index_Buffer(m_indexBuffer,0)",
                      "Renderer::Bind_Legacy_Vertex_Buffer(m_vertexBuffer)",
                      "Renderer::Set_Index_Buffer_Index_Offset(trackStartIndex)"):
            self.assertIn(token, terrain)
        for token in ("RENDER_MAP_NO_OVERWRITE", "RENDER_MAP_DISCARD",
                      "Renderer::Bind_Legacy_Vertex_Buffer(m_vertexBuffer)",
                      "Renderer::Bind_Legacy_Index_Buffer(TheWaterTracksRenderSystem->m_indexBuffer,batchStart)"):
            self.assertIn(token, water)
        self.assertIn("Renderer::Bind_Dynamic_Vertex_Buffer(", particles)
        self.assertIn("Renderer::Bind_Dynamic_Index_Buffer(", particles)
        self.assertIn("Renderer::Draw_Triangle_Strip(", water)
        self.assertIn("Renderer::Bind_Legacy_Vertex_Buffer(vertexBuffer)", water)
        # The mutable DX8 pattern binds once before Map. DX12 must snapshot
        # the newly written range before its subsequent indexed strip draw.
        self.assertLess(water.index("Renderer::Unmap_Vertex_Buffer(bufferHandle);"),
                        water.index("Renderer::Bind_Legacy_Vertex_Buffer(vertexBuffer)"))
        self.assertLess(water.index("Renderer::Bind_Legacy_Vertex_Buffer(vertexBuffer)"),
                        water.index("Renderer::Bind_Legacy_Index_Buffer(TheWaterTracksRenderSystem->m_indexBuffer,batchStart)"))
        smoke = (PRESENTATION / "DX12GeometryBufferSmoke.cpp").read_text(encoding="utf-8")
        for token in ("Bind first borrowed sorting vertex slice",
                      "Bind first borrowed R16 sorting index slice",
                      "Snapshot rewritten CPU sorting vertices",
                      "Borrowed CPU sorting data remains immutable on WARP after owner rewrite"):
            self.assertIn(token, smoke)

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
