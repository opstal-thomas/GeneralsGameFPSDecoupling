#!/usr/bin/env python3
"""DX12-B02: verify Renderer API parity and exclusive source selection.

Windows CI independently examines CMake compile_commands.json for real build
source membership; these tests can run on any operating system.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
WW3D = ROOT / "Core/Libraries/Source/WWVegas/WW3D2"
CMAKE = WW3D / "CMakeLists.txt"
DX8 = WW3D / "Backend/DX8Backend.cpp"
DX12 = WW3D / "Backend/DX12/DX12Backend.cpp"
# R02's two typed W3D-accessor overloads are intentionally implemented next
# to their complete legacy types, to keep D3D8 headers out of DX12Backend.cpp.
DX12_DYNAMIC_ADAPTERS = [
    WW3D / "dx8vertexbuffer.cpp",
    WW3D / "dx8indexbuffer.cpp",
    WW3D / "dx8renderer.cpp",
]

# Definitions only (not calls): all Renderer static methods must have exactly
# one implementation in each backend before they can be switched at link time.
DEFINITION = re.compile(
    r"^([A-Za-z_][\w:<>,\*& \t]*\s+Renderer::[A-Za-z_]\w*"
    r"\([\s\S]*?\))\s*\{", re.MULTILINE
)


def signatures(source: Path) -> list[str]:
    contents = source.read_text(encoding="utf-8")
    return [re.sub(r"\s+", " ", m.group(1).strip())
            for m in DEFINITION.finditer(contents)]


class DX12BackendSelectionTests(unittest.TestCase):
    def test_renderer_contract_is_complete_and_exclusive(self):
        dx8 = signatures(DX8)
        native = signatures(DX12)
        adapters = [signature for path in DX12_DYNAMIC_ADAPTERS
                    for signature in signatures(path)]
        dx12 = native + adapters
        expected_adapters = {
            "void Renderer::Bind_Dynamic_Vertex_Buffer(const DynamicVBAccessClass & access)",
            "void Renderer::Bind_Dynamic_Index_Buffer(const DynamicIBAccessClass & access, unsigned short baseVertexIndex)",
            "void Renderer::Bind_Legacy_Vertex_Buffer(const VertexBufferClass * buffer)",
            "void Renderer::Bind_Legacy_Index_Buffer(const IndexBufferClass * buffer, unsigned short baseVertexIndex)",
            "void Renderer::Flush_Mesh_Batches()",
            "void Renderer::Set_Shader(const ShaderClass & shader)",
            "void Renderer::Set_Texture(unsigned stage, TextureBaseClass * texture)",
        }
        self.assertGreater(len(dx8), 150)
        self.assertEqual(len(dx8), len(set(dx8)), "DX8 defines duplicate signatures")
        self.assertEqual(set(adapters), expected_adapters,
                         "Only the typed W3D buffer/material/mesh adapters may live outside DX12Backend.cpp")
        self.assertEqual(len(adapters), len(set(adapters)))
        self.assertEqual(set(dx8), set(dx12),
                         "Missing, duplicate or extra DX12 Renderer definitions")
        self.assertEqual(len(dx12), len(set(dx12)))
        for path in DX12_DYNAMIC_ADAPTERS:
            code = path.read_text(encoding="utf-8")
            self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", code,
                          "Legacy accessor bridge must compile only for DX12")

    def test_dx12_backend_does_not_delegate_to_dx8(self):
        code = DX12.read_text(encoding="utf-8")
        # Comments describe the DX8 devices we deliberately do NOT create.
        code = re.sub(r"//[^\r\n]*|/\*[\s\S]*?\*/", "", code)
        self.assertNotRegex(code, r"(?m)^\s*#\s*include\s*[<\"]d3d8")
        self.assertNotRegex(code, r"\bDX8Wrapper\s*::")
        self.assertNotRegex(code, r"\bDirect3DCreate8\s*\(")
        self.assertNotRegex(code, r"\bIDirect3D\w*8\b")
        self.assertIn("W3D_RENDER_BACKEND_DX12", code)
        self.assertIn("Renderer::Init(void * window, bool lite)", code)
        self.assertRegex(code, r"(?s)bool Renderer::Init\(.*?\)\s*\{.*?"
                          r"ReportPending\(.*?return false;")

    def test_cmake_selects_exactly_one_renderer_source(self):
        cmake = CMAKE.read_text(encoding="utf-8")
        self.assertIn('set(W3D_RENDER_BACKEND "DX8" CACHE STRING', cmake)
        self.assertIn('if(W3D_RENDER_BACKEND STREQUAL "DX12")', cmake)
        self.assertRegex(
            cmake,
            r'(?s)if\(W3D_RENDER_BACKEND STREQUAL "DX12"\)\s*'
            r'list\(APPEND WW3D2_SRC Backend/DX12/DX12Backend.cpp\)\s*'
            r'else\(\)\s*'
            r'list\(APPEND WW3D2_SRC Backend/DX8Backend.cpp\)\s*endif\(\)'
        )
        self.assertIn("target_compile_definitions(corei_ww3d2 INTERFACE"
                      " W3D_RENDER_BACKEND_DX12=1)", cmake)
        self.assertIn("W3D_ENABLE_DX12_GAMEPLAY_PREVIEW requires "
                      "W3D_RENDER_BACKEND=DX8", cmake)
        self.assertIn("target_link_libraries(corei_ww3d2 INTERFACE "
                      "w3d_dx12_presentation)", cmake)

    def test_named_bootstrap_preset_is_explicit(self):
        import json
        data = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        configured = next(x for x in data["configurePresets"]
                          if x["name"] == "win32-dx12-bootstrap")
        self.assertEqual(configured["cacheVariables"]["W3D_RENDER_BACKEND"], "DX12")
        self.assertEqual(configured["cacheVariables"]
                         ["W3D_ENABLE_DX12_GAMEPLAY_PREVIEW"], "OFF")
        built = next(x for x in data["buildPresets"]
                     if x["name"] == "win32-dx12-bootstrap")
        self.assertEqual(built["configurePreset"], configured["name"])


if __name__ == "__main__":
    unittest.main()
