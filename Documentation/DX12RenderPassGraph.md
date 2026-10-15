# DX12-B05 — Native W3D pass scheduling and GPU resource barriers

**Status:** Native pass-order and resource-state foundation implemented. Actual
opaque/terrain/water/effects/shroud/UI draw submission remains under the
authoritative asset, buffer, draw and offscreen workstreams (R01, R04, R07,
M01 and B07). This is **not** DX12 gameplay or scene parity.

## Canonical frame order

The backend-private `DX12W3DPassGraph` enforces these ordered phases:

| Order | Stage | Future W3D producers |
| --- | --- | --- |
| 1 | `OpaqueWorld` | Buildings, units and other opaque meshes |
| 2 | `Terrain` | Height field, roads, decals and ground layers |
| 3 | `Water` | Water plane, shore effects and sampling |
| 4 | `Effects` | Particle passes, projectors and special effects |
| 5 | `Transparency` | Ordered/translucent meshes and alpha effects |
| 6 | `Shroud` | Shroud, fog-of-war and associated masks |
| 7 | `UI` | HUD, text, cursor and 2D composites |
| 8 | `PostProcess` | Final color grading, resolve and filters |

Passes may be **skipped** (the developer launch may only clear/present), but
may not be repeated or entered out of order within a single frame. The
`DX12PrimaryWindow::BeginScene` native path enters `OpaqueWorld` after
opening the real game-HWND command list; later native submissions use
`DX12PrimaryWindow::BeginPass` /
`DX12PresentationDevice::BeginRenderPass`.

**Important distinction:** The graph records GPU state transitions and stage
boundaries. It does not implicitly bind D3D12 descriptor heaps, create RTV or
DSV descriptors, select a pipeline-state object, or submit W3D game geometry.
Those responsibilities belong to the resource and material providers that
will call into these stages. The current game's resource paths therefore
remain fail-closed for DX12-only gameplay.

## Resource-state contract

`DX12PresentationDevice` owns a single command list for a frame and its
`DX12W3DPassGraph` tracks the real D3D12 state of every registered resource.

- The current swap-chain buffer begins in
  `D3D12_RESOURCE_STATE_PRESENT` and transitions to
  `D3D12_RESOURCE_STATE_RENDER_TARGET` when frame recording starts.
- The persistent native D32 depth buffer begins in
  `D3D12_RESOURCE_STATE_DEPTH_WRITE`. Other depth/texture resources
  must be registered separately once R07 manages their lifetimes.
- Each pass explicitly declares color outputs, depth outputs, shader-read
  inputs, and optionally UAV/compute outputs through
  `DX12W3DPassBindings`.
- Color writes transition to `RENDER_TARGET`, depth writes to
  `DEPTH_WRITE`, sampled textures to combined pixel/nonpixel
  `SHADER_RESOURCE`, and UAV outputs to `UNORDERED_ACCESS`. For UAV
  outputs already in that state, a real UAV memory-order barrier prevents
  unordered write-after-write execution between passes.
- The resource tracker emits a `D3D12_RESOURCE_BARRIER` using the
  **tracked before and requested after state**, not a guessed `COMMON`
  state. Repeated access in the same state avoids redundant transition
  barriers. `BarrierFrameUAV` is available for explicit within-pass
  UAV synchronization between dispatches.
- Unregistered resources, unknown/out-of-order passes, duplicate UAV
  outputs, render-target/depth aliasing, and same-pass SRV/write hazards
  fail **before** any part of the proposed pass is recorded.
- `TransitionFrameResource` supports explicit copy/resolve operations.
  It is required for resources managed by B05: issuing raw untracked
  `ResourceBarrier` calls can desynchronize the graph and is prohibited
  for these resources in future authoritative implementations.
- Every frame returns its current back buffer to
  `D3D12_RESOURCE_STATE_PRESENT`, and restores the persistent D32 depth
  surface to `D3D12_RESOURCE_STATE_DEPTH_WRITE`, even after an offscreen
  copy or shader sampling stage. Optional stages may be skipped, and
  `End_Scene(false)` still submits without DXGI Present.
- Resource registrations are **per frame**. Offscreen resource owners must
  re-register their actual current states after frame completion.
  `Resize` / `Shutdown` invalidate the tracker, preventing stale COM
  resource pointers from surviving a swap-chain recreation.

This first version tracks whole resources using
`D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES`; per-mip/subresource residency and
render-to-texture descriptor/format policy are tracked under R07.

## WARP verification and CI

`W3DDX12PassGraphSmoke` runs on both Win32 and x64 in the existing
[DX12 W3D Shader Validation workflow](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/workflows/dx12-shader-validation.yml).
It creates a real WARP device, primary swap chain, offscreen GPU texture,
readback buffer, and native command list. It verifies all eight stages,
out-of-order rejection, read/write hazard rejection, external resource
registration, real offscreen `RENDER_TARGET → SHADER_RESOURCE → COPY_SOURCE`
transitions, and **GPU pixel readback** of the offscreen RTV clear. It also
exercises UAV output transitions and same-state UAV ordering, compute-only
postprocess, omitted stages, nonflipping frame submission and safe
swap-chain resizing.

`scripts/tests/test_dx12_pass_graph.py` guards the canonical order, native
integration, forbidden DX8 delegation and test/CI coverage. Both it and
the Windows WARP test gate the feature branch.

**Verified 2026-10-16:** [Win32/x64 WARP, GPU pixel readback, shader and
source-contract workflow](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38824254425)
and [all seven Generals/Zero Hour Windows build/audit
jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38824254493)
passed.

### Follow-on integration

B05 establishes the **render-pass contract**. It intentionally does not
claim actual Generals/Zero Hour terrain, water, shroud, effects or UI coverage.
The next work is **R01** (backend-owned vertex/index buffers), **R04**
(native textures), **R07** (offscreen RTV/DSV/SRV residency/targets),
**B07/M01** (route authoritative W3D draws into these stages), followed by
scene/gameplay parity validation under V03.
