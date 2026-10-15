#!/usr/bin/env python3
"""DX12-B10: protect the separate genuine DX12-only developer launch boundary."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import re
import struct
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PRESENTATION = ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/Presentation"
BACKEND = PRESENTATION.parent / "DX12Backend.cpp"


def import_pe_checker():
    script = ROOT / "scripts/check_dx12_launch_pe.py"
    spec = importlib.util.spec_from_file_location("dx12_pe_checker", script)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class B10LaunchTests(unittest.TestCase):
    def test_independent_windows_gui_target_is_dx12_only(self):
        cmake = (PRESENTATION / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("add_executable(W3DDX12LaunchCheckpoint WIN32", cmake)
        target = cmake.split("add_executable(W3DDX12LaunchCheckpoint WIN32", 1)[1]
        self.assertIn("DX12LaunchCheckpoint.cpp", target)
        self.assertIn("../DX12Backend.cpp", target)
        self.assertIn("W3D_DX12_DEVELOPER_CHECKPOINT=1", target)
        self.assertIn("W3D_RENDER_BACKEND_DX12=1", target)
        self.assertIn("W3DDX12PresentationDevice user32 shell32", target)
        self.assertIn("W3D_DX12_WARP_Developer_Launch", target)
        self.assertNotRegex(re.sub(r"#.*", "", target), r"\bd3d8\b|DX8Backend|d3dx8")
        game = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("W3D_DX12_DEVELOPER_CHECKPOINT", game)

    def test_game_backend_keeps_fail_closed_opt_in(self):
        code = BACKEND.read_text(encoding="utf-8")
        self.assertRegex(code, r"(?s)#if defined\(W3D_DX12_DEVELOPER_CHECKPOINT\).*?"
                               r"return true;.*?#else.*?W3D_DX12_B04_DIAGNOSTIC.*?#endif")
        self.assertIn("if (!DiagnosticOptIn())", code)
        self.assertIn("primaryWindow.Shutdown();", code)
        no_comments = re.sub(r"//[^\r\n]*|/\*[\s\S]*?\*/", "", code)
        self.assertNotRegex(no_comments, r"\bDX8Wrapper\s*::|\bDirect3DCreate8\s*\(")

    def test_launcher_calls_native_frame_no_dx8(self):
        code = (PRESENTATION / "DX12LaunchCheckpoint.cpp").read_text(encoding="utf-8")
        self.assertIn("CreateWindowExW(", code)
        self.assertIn("Renderer::Init(app.window, false)", code)
        self.assertIn("Renderer::Clear(true, true,", code)
        self.assertIn("Renderer::Begin_Scene()", code)
        self.assertIn("Renderer::End_Scene(true)", code)
        self.assertIn("Renderer::Shutdown()", code)
        self.assertIn("Renderer::Get_Device_Status()", code)
        self.assertIn("WM_QUIT", code)
        self.assertIn("--frames", code)
        self.assertIn("--warp", code)
        self.assertIn('GetModuleHandleW(L"d3d8.dll") == nullptr', code)
        self.assertIn('GetModuleHandleW(L"d3dx8.dll") == nullptr', code)
        no_comments = re.sub(r"//[^\r\n]*|/\*[\s\S]*?\*/", "", code)
        self.assertNotRegex(no_comments, r"\b(?:IDirect3DDevice8|Direct3DCreate8)\b")

    def test_workflow_requires_actual_built_executable_and_pe_import_audit(self):
        workflow = (ROOT / ".github/workflows/dx12-shader-validation.yml").read_text(encoding="utf-8")
        self.assertIn('foreach ($architecture in @("Win32", "x64"))', workflow)
        self.assertIn("ctest --test-dir $build -C Release --output-on-failure", workflow)
        self.assertIn("python scripts/check_dx12_launch_pe.py --exe $checkpoint", workflow)
        self.assertIn("name: w3d-dx12-b10-launch-checkpoint", workflow)
        self.assertIn("W3DDX12LaunchCheckpoint-$architecture.exe", workflow)

    def test_pe_import_audit_accepts_dx12_and_rejects_dx8(self):
        checker = import_pe_checker()
        for machine, magic, optional_length, directories, image_base in (
            (0x14C, 0x10B, 224, 96, 0x400000),
            (0x8664, 0x20B, 240, 112, 0x140000000),
        ):
            with self.subTest(machine=machine):
                pe = bytearray(2048)
                pe[0:2] = b"MZ"
                struct.pack_into("<I", pe, 0x3C, 0x80)
                pe[0x80:0x84] = b"PE\x00\x00"
                struct.pack_into("<H", pe, 0x84, machine)
                struct.pack_into("<H", pe, 0x86, 1)  # one section
                struct.pack_into("<H", pe, 0x94, optional_length)
                opt = 0x98
                struct.pack_into("<H", pe, opt, magic)
                if magic == 0x10B:
                    struct.pack_into("<I", pe, opt + 28, image_base)
                else:
                    struct.pack_into("<Q", pe, opt + 24, image_base)
                struct.pack_into("<I", pe, opt + 60, 0x400)
                struct.pack_into("<II", pe, opt + directories + 8, 0x1000, 80)
                sect = opt + optional_length
                struct.pack_into("<IIII", pe, sect + 8, 0x400, 0x1000, 0x400, 0x400)
                for offset, name_rva in ((0x400, 0x1100), (0x414, 0x1120)):
                    struct.pack_into("<I", pe, offset + 12, name_rva)
                pe[0x500:0x50A] = b"d3d12.dll\x00"
                pe[0x520:0x529] = b"dxgi.dll\x00"
                with tempfile.TemporaryDirectory() as d:
                    filename = Path(d) / "launch.exe"
                    filename.write_bytes(pe)
                    arch, imports = checker.imports_from_pe(filename)
                    self.assertIn(arch, ("Win32", "x64"))
                    self.assertEqual(imports, ["d3d12.dll", "dxgi.dll"])
                    checker.validate(filename)
                    pe[0x520:0x529] = b"d3d8.dll\x00"
                    filename.write_bytes(pe)
                    with self.assertRaises(checker.PEFormatError):
                        checker.validate(filename)


if __name__ == "__main__":
    unittest.main()
