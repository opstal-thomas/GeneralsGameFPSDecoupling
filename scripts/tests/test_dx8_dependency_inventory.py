#!/usr/bin/env python3
"""Unit tests for the repo-wide DX8 dependency inventory."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dx8_dependency_inventory as audit


class DependencyInventoryTests(unittest.TestCase):
    def test_cpp_finds_legacy_symbols_but_not_comments_or_dx12(self):
        contents = """#include "dx8wrapper.h"
        // DX8Wrapper::Fake()
        const char * text = "D3DRS_ZENABLE";
        /* IDirect3DDevice8 * hidden; */
        DX8Wrapper::Set_Shader(shader);
        IDirect3DTexture8 * texture = nullptr;
        texture->LockRect(0, out, nullptr, 0);
        D3DRS_ZWRITEENABLE;
        D3DXMatrixInverse(matrix, det, old);
        D3D12_RESOURCE_DESC desc = {};
        D3D_FEATURE_LEVEL_11_0;
        D3D_SHADER_MACRO macros;
        D3D_COMPILE_STANDARD_FILE_INCLUDE;
        D3DCOMPILE_OPTIMIZATION_LEVEL3;
        D3D_ROOT_SIGNATURE_VERSION_1;
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        """
        findings = audit.scan_text(
            "Core/GameEngineDevice/Source/W3DDevice/GameClient/example.cpp", contents
        )
        names = {(f["kind"], f["symbol"]) for f in findings}
        self.assertIn(("dx8_include", "dx8wrapper.h"), names)
        self.assertIn(("wrapper_dispatch", "DX8Wrapper::Set_Shader"), names)
        self.assertIn(("direct3d8_com", "IDirect3DTexture8"), names)
        self.assertIn(("d3d8_com_dispatch", "IDirect3DTexture8::LockRect"), names)
        self.assertIn(("legacy_d3d_type", "D3DRS_ZWRITEENABLE"), names)
        self.assertIn(("d3dx_legacy", "D3DXMatrixInverse"), names)
        self.assertNotIn(("wrapper_dispatch", "DX8Wrapper::Fake"), names)
        self.assertFalse(any(
            x in f["symbol"] for f in findings
            for x in ("D3D12", "D3D_FEATURE_LEVEL", "D3D_SHADER_MACRO",
                      "D3D_COMPILE", "D3DCOMPILE", "D3D_ROOT_SIGNATURE",
                      "D3D_PRIMITIVE_TOPOLOGY")
        ))
        self.assertEqual(findings[0]["line"], 1)
        self.assertEqual({f["scope"] for f in findings}, {"game_runtime"})

    def test_commented_include_and_string_not_counted(self):
        content = """/* #include "dx8wrapper.h" */
        // #include <d3d8.h>
        const char * example = "DX8Wrapper::Reset_Device()";
        """
        self.assertEqual(audit.scan_text("Core/foo.cpp", content), [])

    def test_cmake_linkage_counts_and_comments_do_not(self):
        content = """# d3d8.lib unused
        include(cmake/dx8.cmake)
        target_link_libraries(game dx8 d3d8 d3dx8.lib)
        # dx8backend.cpp
        """
        findings = audit.scan_text("CMakeLists.txt", content)
        self.assertEqual(
            [f["symbol"] for f in findings], ["dx8.cmake", "dx8", "d3d8", "d3dx8.lib"]
        )
        self.assertEqual({f["scope"] for f in findings}, {"build_configuration"})

    def test_backend_only_exemption_does_not_cover_mesh_renderer(self):
        self.assertEqual(audit.scope_for(
            "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX8Backend.cpp"),
            "legacy_backend")
        self.assertEqual(audit.scope_for(
            "Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp"),
            "legacy_backend")
        self.assertEqual(audit.scope_for(
            "Core/Libraries/Source/WWVegas/WW3D2/dx8polygonrenderer.cpp"),
            "w3d_runtime")
        self.assertEqual(audit.scope_for(
            "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12GameplayPreview.cpp"),
            "dx12_preview")

    def test_ratcheting_only_fails_on_new_game_and_build_references(self):
        def row(path, scope, symbol):
            return {"path": path, "scope": scope, "kind": "wrapper_dispatch",
                    "symbol": symbol}
        before = [row("mesh.cpp", "w3d_runtime", "DX8Wrapper::Draw")]
        after = before + [
            row("mesh.cpp", "w3d_runtime", "DX8Wrapper::Draw"),
            row("Backend/DX8Backend.cpp", "legacy_backend", "DX8Wrapper::Draw"),
            row("menu.cpp", "game_runtime", "DX8Wrapper::Begin_Scene"),
        ]
        comparison = audit.compare_findings(after, before)
        self.assertEqual(len(comparison["blocking_additions"]), 2)
        self.assertEqual(len(comparison["allowed_additions"]), 1)
        self.assertEqual(comparison["new_references"], 3)
        self.assertEqual(comparison["resolved_references"], 0)
        resolved = audit.compare_findings(before, after)
        self.assertEqual(resolved["new_references"], 0)
        self.assertEqual(resolved["resolved_references"], 3)

    def test_tracked_source_scope_and_exclusions(self):
        self.assertFalse(audit.eligible("Documentation/Old.md"))
        self.assertFalse(audit.eligible("assets/texture.dds"))
        self.assertTrue(audit.eligible("Generals/Code/Foo.cpp"))
        self.assertTrue(audit.eligible("cmake/dx8.cmake"))
        self.assertEqual(audit.scope_for("scripts/foo.py"), "tools_tests")

    def test_build_and_dx8_adapter_references_both_visible(self):
        text = """#include <d3d8.h>
        IDirect3DDevice8 * device;
        """
        adapter = audit.scan_text(
            "Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp", text)
        self.assertEqual({f["scope"] for f in adapter}, {"legacy_backend"})
        self.assertEqual(len(adapter), 2)


if __name__ == "__main__":
    unittest.main()
