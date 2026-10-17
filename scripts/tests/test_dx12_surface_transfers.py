#!/usr/bin/env python3
"""R08 native W3D surface transfer and backbuffer readback invariants."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
DX12 = ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12"


class DX12SurfaceTransferTests(unittest.TestCase):
    def test_public_renderer_has_gpu_backed_surface_paths(self):
        backend = (DX12 / "DX12Backend.cpp").read_text(encoding="utf-8")
        for token in (
            "DX12W3DSurfaceTransfers surfaceTransfers;",
            "surfaceTransfers.ReadRegion(",
            "surfaceTransfers.Lock(",
            "surfaceTransfers.Unlock(",
            "surfaceTransfers.Copy(",
            "surfaceTransfers.Blit(",
            "surfaceTransfers.ToTexture(",
            "RestoreAfterTransfer();",
        ):
            self.assertIn(token, backend)
        for pending in (
            'ReportPending("Renderer::Lock_Surface: R08',
            'ReportPending("Renderer::Unlock_Surface: R08',
            'ReportPending("Renderer::Copy_Surface: R08',
            'ReportPending("Renderer::Blit_Surface: R08',
            'ReportPending("Renderer::Read_Current_Color_Region',
        ):
            self.assertNotIn(pending, backend)

    def test_frame_segment_waits_and_preserves_bound_ia(self):
        device = (DX12 / "DX12PresentationDevice.cpp").read_text(encoding="utf-8")
        geometry = (DX12 / "DX12W3DGeometryBuffers.cpp").read_text(encoding="utf-8")
        self.assertIn("bool DX12PresentationDevice::FlushAndContinue()", device)
        for token in ("Close R08 readback segment",
                      "s.queue->ExecuteCommandLists(1, lists)",
                      "s.WaitForFence(fenceValue)",
                      "Reset R08 segment allocator",
                      "IASetVertexBuffers(i, 1, &s.vertexViews[i])",
                      "IASetIndexBuffer(&s.indexView)"):
            self.assertIn(token, device)
        self.assertIn("TrackVertexBinding(", geometry)
        self.assertIn("TrackIndexBinding(", geometry)

    def test_r07_msaa_depth_and_out_of_bounds_rejected(self):
        targets = (DX12 / "DX12W3DRenderTargets.cpp").read_text(encoding="utf-8")
        transfers = (DX12 / "DX12W3DSurfaceTransfers.cpp").read_text(encoding="utf-8")
        for token in ("bool DX12W3DRenderTargets::TransferResource(",
                      "bool DX12W3DRenderTargets::TransitionTransfer(",
                      "bool DX12W3DRenderTargets::RebindAfterFlush(",
                      "s->IsColor()", "Impl::Kind::MSAAColor",
                      "ResolveSubresource("):
            self.assertIn(token, targets)
        for token in ("ValidRect(", "MaxTransferBytes", "COPY_SOURCE",
                      "COPY_DEST", "GetCopyableFootprints(",
                      "CopyTextureRegion(", "FlushAndContinue()",
                      "Restore(frame, targets", "RetainFrameResource(upload.Get())",
                      "DX12W3DSurfaces::Blit(", "readOnly"):
            self.assertIn(token, transfers)
        self.assertNotIn("IDirect3DSurface8", transfers)
        self.assertNotIn("D3DX", transfers)

    def test_warp_gpu_roundtrip_and_renderer_paths_are_exercised(self):
        native = (DX12 / "Presentation/DX12NativeDrawSmoke.cpp").read_text(encoding="utf-8")
        renderer = (DX12 / "Presentation/DX12RendererFrameSmoke.cpp").read_text(encoding="utf-8")
        for token in (
            "R08 in-frame GPU readback sees completed opaque source",
            "R08 second draw survives flush and shows green",
            "R08 native GPU to GPU CopyTextureRegion",
            "R08 GPU surface read-modify-write lock",
            "R08 half-pixel bilinear native output",
        ):
            self.assertIn(token, native)
        for token in ("R08 GPU RTT read-only lock maps fenced D3D12 pixels",
                      "R08 Renderer screenshot region uses current offscreen GPU target",
                      "R08 GPU surface to native texture conversion"):
            self.assertIn(token, renderer)


if __name__ == "__main__":
    unittest.main()
