# DX12-R08 — native image-surface transfers and framebuffer readback

**Implemented:** R08's supported 32-bit W3D color-surface paths on the
exclusive DX12 backend (2026-10-17). No D3D8 device or `IDirect3DSurface8`
is involved. Gameplay acceptance remains a separate V03 milestone.

## Neutral Renderer entry points

The R08 bridge lives in
`Backend/DX12/DX12W3DSurfaceTransfers.{h,cpp}` and connects the original
opaque `RenderSurfaceHandle` API with R04 CPU image surfaces, R07 native
RTV texture/primary swap-chain surfaces and B05 resource-state barriers:

- `Renderer::Read_Current_Color_Region`: copies the actively selected color
  output, rather than an unrelated primary backbuffer. Validates the entire
  source rectangle and destination byte capacity, reads through a D3D12
  `READBACK` buffer with `GetCopyableFootprints`, synchronizes the GPU via
  a fence and returns tightly packed native W3D A8R8G8B8/BGRA bytes.
  The DX12 R8G8B8A8 swap-chain output is explicitly channel-swizzled.
- `Renderer::Lock_Surface/Unlock_Surface`: R04 CPU handles retain their
  original CPU map behavior; R07 GPU color handles use fenced download and
  a row-pitched CPU shadow. Writable unlock commits that exact region
  with an `UPLOAD` heap and `CopyTextureRegion`. Nested locks, invalid
  rectangles, unowned resources and depth surfaces reject instead of
  interpreting a GPU pointer as host memory.
- `Renderer::Copy_Surface`: fast D3D12 image copy for distinct compatible
  GPU textures; CPU readback+upload for mixed CPU/GPU images, swizzled
  formats and aliasing. Retains the user's source/destination rectangle
  and point offset without changing ownership.
- `Renderer::Blit_Surface`: bounded stretch/copy. W3D 32-bit color
  `RENDER_SURFACE_BLIT_TRIANGLE` uses bilinear sampling at pixel centers;
  `NONE` uses nearest-neighbor. GPU blits use a CPU intermediate with
  upload/download; this is correctness-first and may stall. Packed
  non-32-bit formats retain the existing CPU-only fallback.
- `Renderer::Create_Texture_From_Surface`: an R07 GPU color surface can
  now generate an R04 texture with optional CPU mip generation, from
  actual GPU pixels rather than a stale image shadow.
- `Renderer::Update_Texture`: existing R04 managed texture-to-texture
  updates remain intact; 32-bit single-mip RTT source/destination updates
  are explicitly bridged through current GPU content, including compatible
  managed/attachment transfers.

## GPU synchronization and draw-state recovery

`DX12PresentationDevice::FlushAndContinue` splits *the existing*
game-HWND command list and submits recorded draws/copies, fences their
completion, resets its allocator/list, and resumes recording within the
same game frame. B05 preserves the same pass graph, tracked resource
states and stage ordering. R02 keeps CPU upload pages and native buffer
generations pinned; a new IA tracking cache restores previously bound
vertex/index views after command-list reset. R07 restores the current
offscreen or primary render-target descriptors and appropriate rasterizer
viewport; the primary-window camera viewport is re-applied by Renderer.
Fence-retained resources/descriptors remain live through the final frame
submission. No asynchronous readback pointer or unfenced mapped staging
buffer is exposed to gameplay.

**Cost:** Each synchronous GPU readback incurs a command-list flush and
GPU fence wait. This preserves observable DX8 LockRect/readback semantics,
but frequent screenshot/smudge probes should later use batched or delayed
readback to avoid CPU/GPU bubbles.

## Validation

- `scripts/tests/test_dx12_surface_transfers.py` is an explicit source
  contract gate in *both* DX12/WARP and Win32-build GitHub workflows.
  It checks native ownership, copyable footprints, barriers, bounds,
  original Renderer integration and command-list/IA restoration.
- The actual Win32/x64 WARP `DX12NativeDrawSmoke` verifies rendered
  offscreen pixels, in-frame readback, new draws *after* the flush,
  CPU/GPU image copies, GPU-to-GPU `CopyTextureRegion`, writable GPU
  surface locks and bilinear stretched uploads.
- `DX12RendererFrameSmoke` validates GPU RTV read-only lock, current
  color screenshot, GPU-to-texture conversion, RTT-to-RTT UpdateTexture
  and a primary swap-chain screenshot through the *public* Renderer API.
- [GPU/WARP pass on the full R08 core](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/39037041131)
  and [final screenshot smoke validation](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/39037102963).
- [Both game Win32 build matrix](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/39037103027).

## Deliberate non-goals and acceptance scope

- Only D3D12 single-sample 32-bit color surfaces are writable and directly
  copyable; MSAA render targets first resolve through R07 for readback.
  Depth/stencil maps/blits and writable MSAA resources do not masquerade as
  color surfaces. Compressed CPU texture data continues through R04.
- Native GPU stretch blits currently use a CPU staging copy, not a new
  fullscreen graphics/compute shader. U06 may optimize full-screen effects.
- This milestone does not enable normal DX12-only gameplay. DX12
  terrain/water/shroud, postprocess, complete B07 methods and V03 in-game
  screenshot parity remain separate acceptance gates.
