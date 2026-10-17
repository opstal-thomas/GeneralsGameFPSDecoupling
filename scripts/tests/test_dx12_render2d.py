#!/usr/bin/env python3
"""U01: both games submit actual Render2D/glyph quads through native DX12."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "Core/Libraries/Source/WWVegas/WW3D2"
SOURCES = (
    ROOT / "Generals/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp",
    ROOT / "GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp",
)

class DX12Render2DTests(unittest.TestCase):
    def test_original_render2d_uses_cpu_sorting_geometry_on_dx12(self):
        for path in SOURCES:
            with self.subTest(game=path.parts[-7]):
                source = path.read_text(encoding="utf-8")
                render = source.split("void Render2DClass::Render()\n{", 1)[1].split(
                    "/*\n** Render2DTextClass", 1)[0]
                native = render.split("#if defined(W3D_RENDER_BACKEND_DX12)", 1)[1].split(
                    "#else", 1)[0]
                legacy = render.split("#else", 1)[1].split("#endif", 1)[0]
                for symbol in (
                    "BUFFER_TYPE_DYNAMIC_SORTING",
                    "Renderer::Bind_Dynamic_Vertex_Buffer(vb)",
                    "Renderer::Bind_Dynamic_Index_Buffer(ib, 0)",
                    "Renderer::Set_Texture(0, Texture)",
                    "Renderer::Set_Shader(spriteShader)",
                    "Renderer::Draw_Triangles(",
                    "Renderer::Set_Transform(RENDER_TRANSFORM_VIEW, savedView)",
                    "Renderer::Set_Transform(RENDER_TRANSFORM_PROJECTION, savedProjection)",
                    "Renderer::Unbind_Sorting_Geometry()",
                ):
                    self.assertIn(symbol, native)
                self.assertNotIn("DX8Wrapper::", native)
                self.assertIn("DX8Wrapper::Draw_Triangles(", legacy)
                self.assertIn("BUFFER_TYPE_DYNAMIC_DX8", legacy)

    def test_backend_supports_r02_cpu_fvf_and_opaque_alpha_draw(self):
        backend = (CORE / "Backend/DX12/DX12Backend.cpp").read_text(encoding="utf-8")
        game_draw = (CORE / "Backend/DX12/DX12W3DNativeDraw.cpp").read_text(encoding="utf-8")
        for symbol in ("BindTransientVertex(", "BindTransientIndex(",
                       "nativeDraw.BindVertex(", "nativeDraw.BindIndex("):
            self.assertIn(symbol, backend)
        for symbol in ("DrawIndexedInstanced(", "D3D12_CULL_MODE_NONE",
                       "blend.alphaTestEnabled", "D3D12_BLEND_SRC_ALPHA"):
            self.assertIn(symbol, game_draw)

if __name__ == "__main__":
    unittest.main()
