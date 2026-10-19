#!/usr/bin/env python3
"""U02: W3D sentence font glyphs must upload through actual native DX12."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
WW3D = ROOT / "Core/Libraries/Source/WWVegas/WW3D2"
FONT = (WW3D / "render2dsentence.cpp").read_text(encoding="utf-8")
TEXTURE = (WW3D / "texture.cpp").read_text(encoding="utf-8")
BACKEND = (WW3D / "Backend/DX12/DX12Backend.cpp").read_text(encoding="utf-8")
TARGETS = (WW3D / "Backend/DX12/DX12W3DRenderTargets.cpp").read_text(encoding="utf-8")
SURFACES = (WW3D / "Backend/DX12/DX12W3DSurfaces.cpp").read_text(encoding="utf-8")
SMOKE = (WW3D / "Backend/DX12/Presentation/DX12TextureSmoke.cpp").read_text(
    encoding="utf-8")
DISPLAY_STRING = (
    ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplayString.cpp"
).read_text(encoding="utf-8")
GAME_WINDOW = (
    ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/GUI/W3DGameWindow.cpp"
).read_text(encoding="utf-8")


class DX12FontAtlasTests(unittest.TestCase):
    def test_original_menu_and_display_strings_use_render2d_text_quads(self):
        self.assertIn("Render2DClass *renderer = surface_info.Renderers[renderer_index]", FONT)
        self.assertIn("renderer->Set_Texture (new_texture);", FONT)
        self.assertIn("Renderers[i].Renderer->Render ();", FONT)
        self.assertIn("m_textRenderer.Render();", DISPLAY_STRING)
        self.assertIn("m_textRenderer.Render();", GAME_WINDOW)
        self.assertIn("m_textRenderer.Set_Location(", GAME_WINDOW)

    def test_font_upload_bypasses_r07_only_texture_surface_views_on_dx12(self):
        function = FONT.split("Render2DSentenceClass::Build_Textures ()", 1)[1]
        native = function.split("#if defined(W3D_RENDER_BACKEND_DX12)", 1)[1].split(
            "#else", 1)[0]
        legacy = function.split("#else", 1)[1].split("#endif", 1)[0]
        self.assertIn("TextureClass (curr_surface, MIP_LEVELS_1)", native)
        self.assertIn("new_texture->Peek_Render_Texture()", native)
        self.assertNotIn("new_texture->Get_Surface_Level ()", native)
        self.assertNotIn("DX8Wrapper::", native)
        self.assertIn("TextureClass (desc.Width, desc.Width, WW3D_FORMAT_A4R4G4B4", legacy)
        self.assertIn("new_texture->Get_Surface_Level ();", legacy)
        copy_branch = function.split("#if !defined(W3D_RENDER_BACKEND_DX12)", 1)[1].split(
            "#endif", 1)[0]
        self.assertIn("Renderer::Copy_Surface", copy_branch)

        acquire = BACKEND.split("Renderer::Acquire_Texture_Surface_Level(", 1)[1]
        self.assertIn("renderTargets.AcquireTextureLevel(", acquire[:450])
        self.assertIn("!DX12W3DTextures::IsAttachment(handle)", TARGETS)

    def test_failed_gpu_upload_retries_without_solid_glyph_rectangles(self):
        upload = FONT.split("Render2DSentenceClass::Build_Textures ()", 1)[1].split(
            "Render2DSentenceClass::Draw_Sentence", 1)[0]
        self.assertIn("++index; // Keep CPU surface and pending glyph geometry", upload)
        self.assertIn("PendingSurfaces.Delete (index);", upload)
        self.assertNotIn("PendingSurfaces.Delete_All ();", upload)
        render = FONT.split("Render2DSentenceClass::Render ()", 1)[1].split(
            "Render2DSentenceClass::Set_Base_Location", 1)[0]
        self.assertIn("if (PendingSurfaces.Count () > 0)", render)
        self.assertIn("return;", render)
        self.assertIn("U02: first native glyph atlas converted and bound", FONT)
        self.assertIn("U02: native glyph atlas upload unavailable; will retry", FONT)

    def test_cpu_atlas_conversion_preserves_packed_alpha_with_warp_readback(self):
        source_ctor = TEXTURE.split(
            "TextureClass::TextureClass\n(\n\tSurfaceClass *surface", 1)[1]
        self.assertIn("Renderer::Create_Texture_From_Surface(", source_ctor[:1150])
        self.assertIn("DX12W3DSurfaces::ToTexture(", BACKEND)
        self.assertIn("DX12W3DTextures::ConvertRowToBGRA8(", SURFACES)
        self.assertIn("desc.pixelFormat = 2; // WW3D_FORMAT_A8R8G8B8", SURFACES)
        for token in (
            "glyphSurfaceDesc.pixelFormat = 7",
            "0xFFFF, 0x8FFF", "0x0FFF, 0xF000",
            "CopyMipToReadback(device, T(glyphAtlas), 0, glyphReadback)",
            "glyphReadback.footprint.Footprint.RowPitch",
            "U02 actual WARP glyph atlas BGRA8 matches 4-bit alpha coverage"
        ):
            with self.subTest(token=token):
                self.assertIn(token, SMOKE)


if __name__ == "__main__":
    unittest.main()
