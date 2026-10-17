"""T01 native terrain boundary regressions (GPU pixels tested by WARP native smoke)."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEIGHT = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp").read_text()
ATLAS = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/TerrainTex.cpp").read_text()
GEOM = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12TerrainTileGeometry.h").read_text()
GPU = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/Presentation/DX12NativeDrawSmoke.cpp").read_text()
HEADER = (ROOT / "Core/GameEngineDevice/Include/W3DDevice/GameClient/HeightMap.h").read_text()
BASE = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp").read_text()
WW3D = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/ww3d.cpp").read_text()
DX12 = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12Backend.cpp").read_text()
DX8_WRAPPER = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp").read_text()
DISPLAY = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp").read_text()


class NativeTerrainContract(unittest.TestCase):
    def test_uses_original_cpu_heightmap_not_preview(self):
        start = HEIGHT.index("void HeightMapRenderObjClass::Render(")
        native = HEIGHT[start:HEIGHT.index("#endif", start)]
        self.assertIn("m_vertexBufferBackup", native)
        self.assertIn("m_treeBuffer->setIsTerrain()", native)
        self.assertIn("getVertexBufferBackup(tileX, tileY)", native)
        self.assertIn("RenderDynamicVertexBinding vertex", native)
        self.assertIn("Renderer::Bind_Dynamic_Vertex_Buffer(vertex)", native)
        self.assertIn("Renderer::Bind_Dynamic_Index_Buffer(index, 0)", native)
        self.assertIn("Renderer::Draw_Triangles", native)
        self.assertNotIn("DX8Wrapper::Draw_Triangles", native)
        self.assertNotIn("Preview_DX12_Gameplay_Mesh", native)

    def test_preserves_original_cells_with_partial_right_bottom_tiles(self):
        for phrase in ("tileLength", "columns > tileLength", "rows > tileLength",
                       "(y * tileLength + x) * 4u", "out.push_back(base + 2)",
                       "out.push_back(base + 3)", "out.push_back(base + 1)"):
            self.assertIn(phrase, GEOM)
        self.assertIn("remainingX = m_x - 1", HEIGHT)
        self.assertIn("remainingY = m_y - 1", HEIGHT)
        self.assertIn("DX12TerrainTileGeometry::BuildIndices", HEIGHT)
        self.assertIn("terrainIndices.size() == 6144", GPU)
        self.assertIn("terrainIndices[12] == 128", GPU)

    def test_uses_actual_original_terrain_atlas(self):
        self.assertIn("Set_Texture(0, m_disableTextures ? nullptr : m_stageZeroTexture)", HEIGHT)
        self.assertIn("RENDER_SAMPLER_CLAMP", HEIGHT)
        self.assertIn("Set_Render_Texture(pBaseTex->Peek_Render_Texture())", ATLAS)
        self.assertIn("Renderer::Map_Texture_2D(texture, 0, mapped)", ATLAS)
        self.assertIn("BuildNativeTerrainMips(this, 2)", ATLAS)
        self.assertIn("BuildNativeTerrainMips(this, 4)", ATLAS)

    def test_gpu_pixels_cover_native_atlas_and_depth(self):
        self.assertIn("BindTransientVertex(device, src.data()", GPU)
        self.assertIn("BindTransientIndex(device, terrainIndices.data()", GPU)
        self.assertIn("readback->Map", GPU)
        self.assertIn("two full 32x32 terrain tiles: GPU UV, elevation, clip, and depth pixels", GPU)
        self.assertIn("terrainPass == 1", GPU)
        self.assertIn("for (unsigned tile=0; tile<terrainTiles.size(); ++tile)", GPU)


    def test_native_dynamic_lighting_preserves_original_baked_vertices(self):
        self.assertIn("std::vector<VERTEX_FORMAT> m_nativeDynamicVertices", HEADER)
        self.assertIn("m_nativeDynamicVertices.assign(m_vertexBufferBackup", HEIGHT)
        self.assertIn("m_nativeDynamicVertices.clear();", HEIGHT)
        self.assertIn("m_nativeDynamicVertices.data() +", HEIGHT)
        self.assertIn("doTheDynamicLight(vb, vbMirror", HEIGHT)
        self.assertIn("m_nativeDynamicVertices.empty() ?", HEIGHT)
        self.assertIn("if (m_nativeDynamicVertices.empty())", HEIGHT)
        self.assertIn("On_Frame_Update();", HEIGHT.split("void HeightMapRenderObjClass::Render(", 1)[1])
        self.assertIn("m_nativeDynamicVertices.clear();", HEIGHT.split("void HeightMapRenderObjClass::freeIndexVertexBuffers()", 1)[1])
        self.assertNotIn("T01 uses original baked terrain lighting", HEIGHT)

    def test_dx12_lod_changes_cannot_select_legacy_flatheightmap(self):
        lod = BASE.split("void BaseHeightMapRenderObjClass::adjustTerrainLOD(Int adj)", 1)[1]
        lod = lod.split("void BaseHeightMapRenderObjClass::ReleaseResources()", 1)[0]
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", lod)
        self.assertIn("TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_MAX;", lod)
        self.assertIn("newROBJ = TheHeightMap;", lod)
        self.assertIn("newROBJ = TheFlatHeightMap;", lod)
        self.assertLess(lod.index("TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_MAX;"),
                        lod.index("newROBJ = TheFlatHeightMap;"))


    def test_real_game_scene_probe_is_explicit_and_not_dx8_preview(self):
        self.assertIn("W3D_DX12_T01_GAMEPLAY", DX12)
        self.assertIn("return b04Diagnostic || TerrainGameplayProbeOptIn();", DX12)
        self.assertIn("if (!DiagnosticOptIn())", DX12)
        self.assertIn("primaryWindow.Shutdown();", DX12)
        self.assertIn("W3D_DX12_T01_GAMEPLAY", HEIGHT)
        self.assertIn("Original heightmap frame=", HEIGHT)
        self.assertIn("tiles=%u triangles=%u atlas=%s", HEIGHT)
        self.assertIn("Native game HWND Present count=", DX12)

    def test_dx8_device_creation_stays_disabled_under_dx12_selection(self):
        for name in ("bool DX8Wrapper::Init(", "bool DX8Wrapper::Set_Render_Device(int dev"):
            body = DX8_WRAPPER.split(name, 1)[1].split("#endif", 1)[0]
            self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", body)
            self.assertIn("return false;", body)
        self.assertIn("// BrowserEngine::Initialize receives", DISPLAY)
        self.assertIn("DX8WebBrowser::Initialize();", DISPLAY)
        self.assertIn("#if !defined(W3D_RENDER_BACKEND_DX12)", DISPLAY)

    def test_real_hwnd_matches_native_display_modes(self):
        self.assertIn("EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS", DISPLAY)
        self.assertIn("GetWindowLongPtrW(ApplicationHWnd, GWL_STYLE)", DISPLAY)
        self.assertIn("setWindowed((style & WS_POPUP) == 0);", DISPLAY)
        mode = DISPLAY.split("Int W3DDisplay::getDisplayModeCount()", 1)[1]
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", mode)
        self.assertIn("return 1;", mode.split("#else", 1)[0])

    def test_game_settings_avoid_dx8_render_device_state(self):
        controls = WW3D.split("void WW3D::Set_Texture_Bitdepth(int bitdepth)", 1)[1]
        self.assertIn("NativeTerrainTextureBitdepth", controls)
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", controls)
        self.assertIn("return MULTISAMPLE_MODE_NONE;", controls)
        self.assertIn("return Has_Active_Device() ? 2u : 0u;", DX12)

    def test_dx12_terrain_does_not_register_dx8_cleanup(self):
        self.assertIn("#if !defined(W3D_RENDER_BACKEND_DX12)", BASE)
        self.assertIn("// The DX12 heightmap never registers with the D3D8 reset/cleanup device.", BASE)
        self.assertIn("DX8Wrapper::SetCleanupHook(this);", BASE)


if __name__ == "__main__":
    unittest.main()
