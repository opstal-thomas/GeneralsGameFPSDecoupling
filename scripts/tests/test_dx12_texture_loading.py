#!/usr/bin/env python3
"""R04: native W3D texture/CPU image loading contract and WARP GPU residency."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
WW3D = ROOT / "Core/Libraries/Source/WWVegas/WW3D2"
DX12 = WW3D / "Backend/DX12"
PRESENTATION = DX12 / "Presentation"

class R04NativeTextureTests(unittest.TestCase):
    def test_renderer_uses_native_backend_for_w3d_asset_maps(self):
        backend = (DX12 / "DX12Backend.cpp").read_text(encoding="utf-8")
        expected = {
            "Create_Texture_2D": "Create2D",
            "Create_Texture_Cube": "CreateCube",
            "Create_Texture_Volume": "CreateVolume",
            "Get_Texture_Mip_Count": "MipCount",
            "Map_Texture_2D": "Map2D",
            "Map_Texture_Cube": "MapCube",
            "Map_Texture_Volume": "MapVolume",
            "Unmap_Texture_2D": "Unmap2D",
            "Unmap_Texture_Cube": "UnmapCube",
            "Unmap_Texture_Volume": "UnmapVolume",
            "Update_Texture": "Update",
            "Retain_Texture": "Retain",
            "Release_Texture": "Release",
            "Bind_Texture": "Prepare",
            "Get_Texture_Priority": "GetPriority",
            "Set_Texture_Priority": "SetPriority",
            "Create_Surface": "Create",
            "Load_Surface": "LoadTGAFile",
            "Create_Texture_From_Surface": "ToTexture",
            "Lock_Surface": "Lock",
            "Unlock_Surface": "Unlock",
            "Copy_Surface": "Copy",
            "Blit_Surface": "Blit",
        }
        for method, internal in expected.items():
            regex = rf"Renderer::{method}\([^{{]*\)\s*\{{(.*?)\}}"
            match = re.search(regex, backend, re.S)
            self.assertIsNotNone(match, method)
            self.assertIn(internal, match.group(1), method)
            self.assertNotIn("ReportPending(", match.group(1), method)
        self.assertIn("D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION", backend)
        self.assertIn("D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION", backend)
        self.assertNotIn("Direct3DCreate8(", backend)

    def test_bc_palette_and_uncompressed_mip_conversion(self):
        s = (DX12 / "DX12W3DTextures.cpp").read_text(encoding="utf-8")
        h = (DX12 / "DX12W3DTextures.h").read_text(encoding="utf-8")
        fmt = (WW3D / "ww3dformat.h").read_text(encoding="utf-8")
        order = re.search(r"enum WW3DFormat\s*\{(.*?)\}", fmt, re.S).group(1)
        names = re.findall(r"WW3D_FORMAT_([A-Z0-9]+)", order)
        mapped = re.search(r"enum W3D\s*:\s*unsigned\s*\{(.*?)\}", s, re.S).group(1)
        local = re.findall(r"\b(?:Unknown|R8G8B8|A8R8G8B8|X8R8G8B8|R5G6B5|X1R5G5B5|A1R5G5B5|A4R4G4B4|R3G3B2|A8|A8R3G3B2|X4R4G4B4|A8P8|P8|L8|A8L8|A4L4|U8V8|L6V5U5|X8L8V8U8|DXT1|DXT2|DXT3|DXT4|DXT5|FormatCount)\b", mapped)
        self.assertEqual(len(names), len(local))
        self.assertEqual([x.upper() for x in local], names)
        for t in (
            "DXGI_FORMAT_BC1_UNORM", "DXGI_FORMAT_BC2_UNORM",
            "DXGI_FORMAT_BC3_UNORM", "DXGI_FORMAT_B8G8R8A8_UNORM",
            "D3D12_HEAP_TYPE_DEFAULT", "D3D12_HEAP_TYPE_UPLOAD",
            "D3D12_RESOURCE_STATE_COPY_DEST", "GetCopyableFootprints(",
            "CopyTextureRegion(", "TransitionFrameResource(",
            "RetainFrameResource(", "std::scoped_lock",
            "ConvertPaletteToBGRA8", "ConvertRowToBGRA8",
            "D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE",
        ):
            self.assertIn(t, s, t)
        self.assertIn("RenderTexture2DHandle Create2D", h)
        self.assertIn("RenderTextureCubeHandle CreateCube", h)
        self.assertIn("RenderTextureVolumeHandle CreateVolume", h)

    def test_cpu_surface_and_missing_image_do_not_create_dx8_devices(self):
        surfaces = (DX12 / "DX12W3DSurfaces.cpp").read_text(encoding="utf-8")
        fallback = (WW3D / "missingtexture.cpp").read_text(encoding="utf-8")
        for token in (
            "DX12W3DTextures::ConvertRowToBGRA8",
            "DX12W3DTextures::Create2D",
            "DX12W3DTextures::Map2D",
            "DX12W3DTextures::Unmap2D",
            "LoadTGAFile(",
            "const bool rle =",
            "paletteOffset",
            "Surface & surface",
        ):
            self.assertIn(token, surfaces, token)
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", fallback)
        self.assertIn("Renderer::Map_Texture_2D(native, mip, mapped)", fallback)
        self.assertIn("Renderer::Unlock_Surface(native)", fallback)
        self.assertIn("Renderer::Release_Texture(", fallback)
        self.assertIn("Renderer::Retain_Texture(", fallback)

    def test_warp_both_architectures_and_real_gpu_readback(self):
        cmake = (PRESENTATION / "CMakeLists.txt").read_text(encoding="utf-8")
        game = (WW3D / "CMakeLists.txt").read_text(encoding="utf-8")
        smoke = (PRESENTATION / "DX12TextureSmoke.cpp").read_text(encoding="utf-8")
        for t in ("DX12W3DTextures.cpp", "DX12W3DSurfaces.cpp"):
            self.assertIn(t, cmake)
            self.assertIn(t, game)
        self.assertIn("W3D_DX12_WARP_W3D_Texture_Upload", cmake)
        for t in ("MapCube(", "MapVolume(", "DXGI_FORMAT_BC1_UNORM",
                  "DXGI_FORMAT_BC3_UNORM", "CopyTextureRegion(",
                  "EqualsReadback(", "ConvertPaletteToBGRA8(",
                  "DX12W3DSurfaces::LoadTGAFile(", "DX12W3DSurfaces::ToTexture(",
                  "device.Shutdown()", "device.Initialize(",
                  "device.InFlightResourceCount()"):
            self.assertIn(t, smoke, t)
        workflow = (ROOT / ".github/workflows/dx12-shader-validation.yml").read_text(encoding="utf-8")
        self.assertIn('foreach ($architecture in @("Win32", "x64"))', workflow)
        self.assertIn("ctest --test-dir $build -C Release --output-on-failure", workflow)

if __name__ == "__main__":
    unittest.main()
