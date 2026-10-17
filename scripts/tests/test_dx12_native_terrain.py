"""T01 native terrain boundary regressions (GPU pixels tested by WARP native smoke)."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEIGHT = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp").read_text()
ATLAS = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/TerrainTex.cpp").read_text()
GEOM = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12TerrainTileGeometry.h").read_text()
GPU = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/Presentation/DX12NativeDrawSmoke.cpp").read_text()


class NativeTerrainContract(unittest.TestCase):
    def test_uses_original_cpu_heightmap_not_preview(self):
        start = HEIGHT.index("void HeightMapRenderObjClass::Render(")
        native = HEIGHT[start:HEIGHT.index("#endif", start)]
        self.assertIn("m_vertexBufferBackup", native)
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
        self.assertIn("BindTransientVertex(device, terrainVertices.data()", GPU)
        self.assertIn("BindTransientIndex(device, terrainIndices.data()", GPU)
        self.assertIn("readback->Map", GPU)
        self.assertIn("terrain UV/coverage and depth WARP pixels", GPU)
        self.assertIn("terrainPass == 1", GPU)


if __name__ == "__main__":
    unittest.main()
