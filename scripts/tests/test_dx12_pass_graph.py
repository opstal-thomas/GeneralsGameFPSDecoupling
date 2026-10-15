#!/usr/bin/env python3
"""B05: guard canonical native W3D pass scheduling and resource barriers."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "Core/Libraries/Source/WWVegas/WW3D2"
DX12 = BASE / "Backend/DX12"
PRESENTATION = DX12 / "Presentation"
STAGES = (
    "OpaqueWorld",
    "Terrain",
    "Water",
    "Effects",
    "Transparency",
    "Shroud",
    "UI",
    "PostProcess",
)


class DX12B05PassGraphTests(unittest.TestCase):
    def test_ordered_pass_contract(self):
        header = (DX12 / "DX12W3DPassGraph.h").read_text(encoding="utf-8")
        names = re.search(
            r"enum class DX12W3DPass\s*:\s*unsigned\s*{([^}]*)}", header, flags=re.S
        )
        self.assertIsNotNone(names)
        stages = re.findall(r"\b[A-Za-z]\w*\b", re.sub(r"//[^\n]*", "", names.group(1)))
        self.assertEqual(stages, list(STAGES) + ["Count"])
        code = (DX12 / "DX12W3DPassGraph.cpp").read_text(encoding="utf-8")
        self.assertIn("order <= currentPass_", code)
        self.assertIn("currentPass_ = order", code)
        self.assertIn("D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE", code)
        self.assertIn("D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE", code)
        self.assertIn("D3D12_RESOURCE_STATE_DEPTH_WRITE", code)
        self.assertIn("D3D12_RESOURCE_STATE_PRESENT", code)
        self.assertIn("D3D12_RESOURCE_STATE_RENDER_TARGET", code)
        self.assertIn("D3D12_RESOURCE_STATE_UNORDERED_ACCESS", code)
        self.assertIn("ResourceBarrier(1, &barrier)", code)
        self.assertIn("input == bindings.colorTarget", code)
        self.assertIn("input == bindings.depthTarget", code)
        self.assertIn("Resource must be registered", code)
        self.assertIn("uavOutputs", code)
        self.assertIn("Simultaneous UAV write and shader-read hazard", code)
        self.assertIn("UAVBarrier(output)", code)
        self.assertIn("A pass must declare a color, depth or UAV output", code)
        self.assertIn("states_.clear()", code)

    def test_actual_dx12_presentation_owns_transitions(self):
        header = (DX12 / "DX12PresentationDevice.h").read_text(encoding="utf-8")
        source = (DX12 / "DX12PresentationDevice.cpp").read_text(encoding="utf-8")
        primary = (DX12 / "DX12PrimaryWindow.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "DX12W3DPassGraph.h"', header)
        self.assertIn("DX12W3DPassGraph passGraph;", source)
        self.assertIn("state.passGraph.BeginFrame(", source)
        self.assertIn("state.passGraph.EndFrame()", source)
        self.assertIn("state.passGraph.BeginPass(", source)
        self.assertIn("state.passGraph.RequireState(", source)
        self.assertIn("state.passGraph.Reset()", source)
        self.assertIn("bool DX12PresentationDevice::BeginRenderPass(", source)
        self.assertIn("bool DX12PresentationDevice::TransitionFrameResource(", source)
        self.assertIn("DX12W3DPass::OpaqueWorld, mainPass", primary)
        self.assertIn("device_.BeginRenderPass(stage, bindings)", primary)
        self.assertIn("device_.EndFrame(syncInterval, flipFrame)", primary)
        self.assertNotRegex(source, r"\bDX8Wrapper\s*::")

    def test_win32_x64_warp_executes_canonical_stage_gpu_readback(self):
        cmake = (PRESENTATION / "CMakeLists.txt").read_text(encoding="utf-8")
        smoke = (PRESENTATION / "DX12PassGraphSmoke.cpp").read_text(encoding="utf-8")
        game_cmake = (BASE / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("../DX12W3DPassGraph.cpp", cmake)
        self.assertIn("Backend/DX12/DX12W3DPassGraph.cpp", game_cmake)
        self.assertIn("W3D_DX12_WARP_Ordered_Render_Passes", cmake)
        for stage in STAGES:
            self.assertIn("DX12W3DPass::" + stage, smoke)
        self.assertIn("D3D12_RESOURCE_STATE_COPY_SOURCE", smoke)
        self.assertIn("ClearRenderTargetView(", smoke)
        self.assertIn("CopyTextureRegion(", smoke)
        self.assertIn("VerifyReadback(", smoke)
        self.assertIn("device.Resize(128, 80)", smoke)
        self.assertIn("Postprocess UAV-only pass", smoke)
        self.assertIn("Transparency stage orders consecutive UAV writes", smoke)
        self.assertIn("Reject out-of-order", smoke)
        workflow = (ROOT / ".github/workflows/dx12-shader-validation.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn('foreach ($architecture in @("Win32", "x64"))', workflow)
        self.assertIn("ctest --test-dir $build -C Release --output-on-failure", workflow)


if __name__ == "__main__":
    unittest.main()
