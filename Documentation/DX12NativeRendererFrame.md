# DX12-B04 — Native renderer frame/state checkpoint

**Status:** Native Renderer lifecycle and state **implemented and WARP validated**.
This is a renderer diagnostic milestone, **not** a playable DX12-only game.
The selected backend never creates an IDirect3D8 device or falls back to
`DX8Wrapper`. Normal DX8 gameplay and the optional DX8-owned preview remain
separate, unchanged configurations.

## Ownership and API

`WW3D::Init` passes the application's borrowed top-level HWND to
`Renderer::Init`. A single `DX12PrimaryWindow` owns the D3D12 presentation
device, flip-discard swap chain, D32 depth attachment, RTV/DSV descriptors,
per-buffer command allocators, command list and fences. The game owns the
HWND, WndProc, input and message pump.

The DX12-selected `Renderer` provides:

- Persistent `Init` / `Shutdown`, device status, native adapter identity,
  thread ID, swap interval, selected adapter and client-resolution queries.
- `Set_Render_Device` / `Set_Device_Resolution` on the borrowed game HWND.
  Accepts a single DX12 adapter, 32-bit swap chain and the existing
  windowed/borderless client mode. An explicit unsupported fullscreen mode
  switch or other device mode fails instead of pretending to succeed.
- `Begin_Scene` / `End_Scene`: native command recording, transitions,
  fence-backed submission and conditional DXGI Present.
  `End_Scene(false)` submits without presenting; `End_Scene(true)`
  submits and presents exactly once.
- Frame clearing. **Important W3D ordering:** `WW3D::Begin_Render` calls
  `Renderer::Clear` **before** `Renderer::Begin_Scene`.
  Pending color and depth clears are independently queued and applied to
  the next native command list; subsequent in-scene clears are immediate.
  No unconditional clear overrides a frame whose caller requested none.
- Backend-owned pixel-space viewport and scissor setup, automatic restoration
  at frame begin, cached world/view/projection transforms, identity reset and
  ARGB color packing.
- Minimize/zero-client-area handling without unsafe presentation; restored
  client sizes resize the original swap chain. Invalid/destroyed windows and
  failed GPU calls do not fall back to DX8.

The lower-level `DX12W3DIndexedMeshPass` independently demonstrates
actual native indexed geometry and texture/shader submission. This is not
yet authoritative WW3D draw submission; that is scheduled under
DX12-R01/R02/M01 and DX12-B07. B04 establishes the **frame/state seam**,
not complete mesh, terrain, water, UI, shadows or effects.

## Diagnostic activation

The `DX12` backend is selected **at CMake configure time**, with
`W3D_RENDER_BACKEND=DX12`.

For developer-only native-frame integration, explicitly set:

~~~text
W3D_DX12_B04_DIAGNOSTIC=1
~~~

To force a software GPU on a test machine, also set:

~~~text
W3D_DX12_GAME_WARP=1
~~~

Without the diagnostic variable, `Renderer::Init` deliberately rejects the
unported game's rendering path and releases the device. Setting the variable
does **not** enable playable Generals/Zero Hour DX12 gameplay: the game's
resource and draw calls remain partly DX8-dependent.

## Verification

- Standalone `W3DDX12RendererFrameSmoke` compiles the **actual**
  `DX12Backend.cpp` against the real neutral `Renderer.h`. It exercises
  fail-closed default initialization, WARP init, transforms, adapter queries,
  native frame submission, resolution changes, and minimize/restore.
- `W3DDX12PrimaryWindowSmoke` tests borrowed-HWND lifetime, resolution
  changes, borderless/windowed, suspend/recovery, queued pre-scene clears,
  in-scene clears, nonflipping submission and **GPU pixel readback**.
- `W3DDX12PresentationSmoke` and the DX12 WARP draw probe verify the
  native swap chain, geometry pass, shader compilation and index handling.
- `scripts/tests/test_dx12_primary_window.py` asserts the native entrypoint
  wiring and the continued absence of DX8 backend delegation.

**Win32 and x64 WARP + shaders:** [passing workflow run](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38819444943).
The game-build matrix separately validates both Generals and Zero Hour in
DX8, DX8+preview and exclusive DX12 bootstrap configurations.

## Deferred milestones

- **B05:** Ordered opaque/transparent/UI/shroud/world passes and barriers.
- **B06:** Fullscreen mode policy, device removal/recovery and GPU timeouts.
- **R01–R04, M01, B07:** Authoritative native buffers, textures, materials
  and all gameplay `Renderer` methods.
- **B09:** Precompiled production shader permutations (the experimental
  mesh pass still compiles loose HLSL).
- **B10 (implemented separately):** The [DX12-only developer launcher](DX12OnlyLaunchCheckpoint.md)
  compiles the same Renderer source in an isolated GUI process, creates its
  main HWND, presents native frames, and verifies absence of D3D8 imports.
  This does not remove the remaining W3D gameplay dependencies.
