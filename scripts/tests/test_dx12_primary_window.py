#!/usr/bin/env python3
"""Guard DX12-B03 primary-game-window wiring and WARP lifecycle test coverage."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
DX12 = ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12"


class PrimaryWindowIntegrationTests(unittest.TestCase):
    def test_game_selected_backend_binds_primary_hwnd_without_fallback(self):
        source = (DX12 / "DX12Backend.cpp").read_text(encoding="utf-8")
        self.assertIn("primaryWindow.Attach(window, useWarp)", source)
        self.assertIn("primaryWindow.PresentDiagnostic(diagnosticColor)", source)
        self.assertIn("primaryWindow.Shutdown()", source)
        self.assertIn('return false;', source.split("bool Renderer::Init", 1)[1].
                      split("void Renderer::Shutdown", 1)[0])
        # Primary window adapter does not use the DX8 preview or its own HWND.
        self.assertNotIn("DX12GameplayPreview::Instance()", source)

    def test_primary_adapter_borrows_not_creates_main_window(self):
        header = (DX12 / "DX12PrimaryWindow.h").read_text(encoding="utf-8")
        code = (DX12 / "DX12PrimaryWindow.cpp").read_text(encoding="utf-8")
        self.assertRegex(header, r"\bbool Attach\(void \* applicationWindow, bool useWarp\)")
        self.assertIn("GetWindowThreadProcessId(hwnd, &processId)", code)
        self.assertIn("GetAncestor(hwnd, GA_ROOT)", code)
        self.assertIn("GetClientRect(hwnd, &client)", code)
        self.assertIn("IsIconic(hwnd)", code)
        self.assertIn("device_.Resize(clientWidth, clientHeight)", code)
        self.assertIn("device_.Initialize(hwnd, clientWidth, clientHeight, warp_)", code)
        self.assertIn("device_.BeginFrame(clearColor)", code)
        self.assertIn("device_.EndFrame(0)", code)
        code_without_comments = re.sub(r"//[^\r\n]*|/\*[\s\S]*?\*/", "", code)
        self.assertNotRegex(code_without_comments,
                            r"\b(?:CreateWindowExW?|DestroyWindow|SetWindowLongPtr)\s*\(")
        self.assertNotRegex(code_without_comments,
                            r"\b(?:DX8Wrapper|IDirect3DDevice8|Direct3DCreate8)\b")

    def test_b04_native_frame_is_explicitly_opt_in(self):
        source = (DX12 / "DX12Backend.cpp").read_text(encoding="utf-8")
        device = (DX12 / "DX12PresentationDevice.cpp").read_text(encoding="utf-8")
        self.assertIn("W3D_DX12_B04_DIAGNOSTIC", source)
        self.assertIn("primaryWindow.Presentation().BeginFrame(black)", source)
        self.assertIn("primaryWindow.Presentation().EndFrame(syncInterval)", source)
        self.assertIn("primaryWindow.Presentation().ClearActiveFrame(", source)
        self.assertIn("RSSetViewports(1, &vp)", source)
        self.assertIn("RSSetScissorRects(1, &scissor)", source)
        self.assertIn("ClearRenderTargetView(rtv, rgba", device)
        self.assertIn("ClearDepthStencilView(", device)

    def test_real_warp_test_and_build_both_include_primary_adapter(self):
        main = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/CMakeLists.txt").read_text(encoding="utf-8")
        smoke = (DX12 / "Presentation/CMakeLists.txt").read_text(encoding="utf-8")
        test = (DX12 / "Presentation/DX12PrimaryWindowSmoke.cpp").read_text(encoding="utf-8")
        self.assertIn("Backend/DX12/DX12PrimaryWindow.cpp", main)
        self.assertIn("../DX12PrimaryWindow.cpp", smoke)
        self.assertIn("W3D_DX12_WARP_PrimaryWindow_Lifecycle", smoke)
        self.assertIn("primary.Attach(game.hwnd, true)", test)
        self.assertIn("SW_MINIMIZE", test)
        self.assertIn("SW_RESTORE", test)
        self.assertIn("WS_POPUP", test)
        self.assertIn("IsBorderless()", test)
        self.assertIn("DestroyWindow(game.hwnd)", test)


if __name__ == "__main__":
    unittest.main()
