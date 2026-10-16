# DX8 to DX12: complete-renderer migration backlog

> **Baseline:** `feature/w3d-renderer-backend-foundation` @ `f86fb79f88bcc04123e28e2e83a13c8576903137` (2026-10-15). This file is a source-backed planning snapshot, not a claim of DX12-only gameplay support.
>
> **Status at baseline:** Generals / Zero Hour normal + DX12-preview Win32 builds and DX12 WARP/shader validation passed. The DX8 reference remains the playable game renderer; DX12 initially used a second preview window.
>
> **B04 update (2026-10-16):** B01–B04 completed the native DX12 frame/state foundation, with passing [game build/audit CI](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38819444975) and [Win32/x64 WARP frame CI](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38819444943).
>
> **B10 update (2026-10-16):** The [dedicated DX12-only developer executable](DX12OnlyLaunchCheckpoint.md) now builds and presents on its primary HWND using the actual DX12 Renderer, with [Win32/x64 WARP, shader, import and source-contract checks passing](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38821817682). The **Generals/Zero Hour game** still cannot run DX12-only gameplay: W3D assets and draw entry points are unported.
>
> **B05 update (2026-10-16):** The [native render-pass/state graph](DX12RenderPassGraph.md) now sequences eight canonical W3D stages with explicit RT/depth/SRV/UAV transitions, read-write hazard checks and frame-end PRESENT/depth restoration. [Win32/x64 WARP passes including GPU readback](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38824254425) and [all seven Generals/Zero Hour build/audit jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38824254493) passed. Real game draw/asset integration remains open.
>
> **R01 update (2026-10-16):** [DX12-owned W3D static and dynamic vertex/index resources](DX12NativeGeometryBuffers.md) now implement neutral Renderer handle creation, byte-range mapping and IA binding, with GPU-local static allocations, upload-backed dynamic generations and fence-safe retirement. [Win32/x64 WARP GPU readback and renderer API smoke](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38826379094) and [all seven Windows game-build/audit jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38826379107) passed. R02 dynamic rings and M01/B07 draw integration remain open.

## How to use this index

- Choose a stable ID such as `DX12-B02`, `DX12-R04`, or `DX12-T05` for a new work branch or issue.
- Every row represents an independently reviewable **work package**, not necessarily one commit. Split a large (`L` or `XL`) row into smaller implementation PRs while keeping the ID.
- `P0`: needed for the first usable DX12-only game or a gating diagnostic; `P1`: required to match the DX8 feature set before complete retirement. Priority is not the execution order; follow the dependencies.
- Effort `S/M/L/XL` is comparative implementation complexity, **not** a time estimate. `Partial: preview` means a proof of concept exists but the authoritative backend is still missing. `Boundary only` means a neutral interface exists but does not execute natively on DX12.
- A task is only complete when its **acceptance check** passes in both Generals and Zero Hour where applicable. Mark its row `Done` after a tested commit/PR, and link the evidence.
- The source audit is not an exhaustive automated proof of every remaining DX8 symbol. `DX12-B01` builds that inventory. New blockers discovered during implementation must be added to this index with new stable IDs.

## Completed backend foundations

- **DX12-B01:** [Source-level DX8 dependency inventory](DX8DependencyInventory.md) and a CI regression baseline.
- **DX12-B02:** [Exclusive DX8/DX12 Renderer selection](DX12ExclusiveBackendBootstrap.md), verified by [Generals and Zero Hour CI](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38809550457).
- **DX12-B03:** [Primary game-HWND presentation](DX12PrimaryGameWindow.md), verified by [all seven game build/audit jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38814822896) and [Win32/x64 WARP primary-window lifecycle tests](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38814822880). B04 now extends this with persistent native frame/state recording.
- **DX12-B04:** [Native Renderer frame/state checkpoint](DX12NativeRendererFrame.md), verified by [seven Generals/Zero Hour Windows build/audit jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38819444975) and [Win32/x64 WARP/shader render, preframe-clear and game-HWND frame tests](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38819444943). Full game draw/asset migration remains B07/R01/R04/M01.
- **DX12-B05:** [Native W3D pass graph and tracked resource-state transitions](DX12RenderPassGraph.md), verified by [Win32/x64 WARP offscreen readback, UAV/depth and ordering tests](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38824254425) and [seven Windows build/audit jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38824254493). Canonical stages are available to future native W3D asset/draw producers; game rendering remains unported.
- **DX12-R01:** [Backend-owned native vertex/index buffers](DX12NativeGeometryBuffers.md), including static DEFAULT heaps, dynamic UPLOAD heap generations, byte-range map/unmap, real IA binding, stale-view unbinding and per-frame GPU fence retention. [Win32/x64 WARP GPU readback and Renderer API tests](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38826379094), and [all seven Windows build/audit jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38826379107) passed. Dynamic ring pooling (R02), nontrivial vertex layouts (R03) and full native draw submission (M01/B07) remain outstanding.
- **DX12-B10:** [DX12-only standalone developer launch](DX12OnlyLaunchCheckpoint.md) verified by [Win32/x64 WARP, PE import and source-contract CI](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38821817682). The primary developer HWND presents without a D3D8 module or import; normal game executables remain fail-closed.

## What already works

- A backend-neutral `Renderer.h` exists and many previously direct DX8 calls route into `DX8Backend.cpp`.
- The exclusive DX12 backend now starts a canonical opaque-world pass per primary-HWND frame and exposes B05 transitions for the eight ordered stages, offscreen targets, shader reads, UAV writes and GPU barrier auditing; actual W3D draws are not yet routed through it.
- DX12 has a tested WARP/hardware-capable device, swap chain, depth attachment, indexed mesh pass, shader combiners, texture uploads and basic alpha blending.
- The **secondary** preview mirrors actual W3D rigid meshes, some multi-material submesh ranges, terrain tiles and water; it now supports configurable 1280x720 default resolution, resizing and an aspect-preserving viewport.
- Preview graphics are obtained from DX8-owned geometry/resources and textures are in part read back from DX8; this is not a DX12-only rendering implementation.
- The preview has diagnostic budgets (1024 draw submissions/frame, 512 cached meshes, 32 MiB retained CPU geometry, 64 captured texture handles and downsampled texture captures); a zero `skipped` counter excludes unsupported geometry never submitted to the preview.
- The DX8 reference implementation, original content/assets, main window and presentation still operate as before.

## Critical path / proposed sequence

**Gate 1 — DX12-only developer launch (verified):** `B01 → B02 → B03 → B04 → B10`. B03 binds the real game HWND in isolation; B10 runs a separate DX12-only developer GUI, with a main HWND and a real native present, without a D3D8 device. This is a **renderer checkpoint**, not a playable Generals/Zero Hour game.

**Gate 2 — playable world:** `R01 → R02/R03`, `R04 → R05/R07`, `M01 → M02/M03/M04/M05`, `T01/T06/T07`, and `U01 → U02/U03/U04/U05`. Complete `V03` (one single-player skirmish) without a D3D8 device. Workstreams can progress in parallel after the backend seam exists.

**Gate 3 — full parity and removal:** finish remaining terrain/water/shadows, multi-pass shading, animation, FX, fullscreen filters, video and robustness, then `V07 → V08`. Do not remove DX8 merely because the mesh preview resembles a scene.

**Next priorities after R01:** `R04` (DX8-independent W3D texture creation/upload), `R02` (efficient mapped dynamic geometry rings and offsets), `R07` (offscreen attachments and resource lifetime), then `M01/B07` (authoritative scene submission using native buffers). `B06` adds device/recovery robustness. The successful B10 developer launcher and R01 buffer handles do **not** imply a playable DX12 match (`V03`).

## Backlog

### B. Backend selection, lifecycle, and frame ownership

| ID | Priority | Effort | Depends on | Current state | Scope and acceptance check |
|---|---|---|---|---|---|
| **DX12-B01** | P0 | S | — | Done (source inventory + CI) | **Inventory every remaining DX8 caller, type, and link dependency.** Implemented with [source inventory](DX8DependencyInventory.md), JSON/Markdown CI artifact and immutable baseline ratchet; binary/runtime proof remains DX12-V07. |
| **DX12-B02** | P0 | M | `DX12-B01` | Done (exclusive backend selection) | **Introduce exclusive DX8 / DX12 renderer build and runtime selection.** [Implemented](DX12ExclusiveBackendBootstrap.md): DX8 and DX12 build choices exclusively compile one Renderer definition set, enforced in Windows compile-command CI; full Generals/Zero Hour DX8, preview and DX12-bootstrap Win32 builds pass. Without the B04 diagnostic opt-in the DX12 executable still deliberately rejects gameplay initialization until the authoritative resource and draw migration. |
| **DX12-B03** | P0 | L | `DX12-B02` | Done (native main-HWND presentation) | **Give DX12 ownership of the game's actual HWND and swap chain.** [Implemented](DX12PrimaryGameWindow.md): the exclusive DX12 backend attaches to the actual game HWND, creates a swap chain and presents a diagnostic frame; client-size changes, minimize/restore, borderless/windowed modes and teardown pass WARP on Win32 and x64. [B04](DX12NativeRendererFrame.md) builds persistent frame/state operations on this HWND; gameplay parity remains outside B03. |
| **DX12-B04** | P0 | L | `DX12-B02`, `DX12-B03` | **Done (native frame/state diagnostic; verified)** | **Implement native DX12 Renderer frame and state entry points.** [Implemented](DX12NativeRendererFrame.md): persistent game-HWND DX12 initialization, native Begin/End scene, conditional Present, queued/in-frame clears, viewport/scissor, transforms, device and client-size queries without DX8Wrapper; lower-level mesh probe demonstrates native indexed rendering. [Win32/x64 WARP validation](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38819444943) and [seven Generals/Zero Hour build/audit checks](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38819444975) passed. Full W3D draw/resource parity remains B07/R01/M01, device-mode robustness B06 and actual game launch/parity under V03. |
| **DX12-B05** | P0 | L | `DX12-B04` | **Done (native pass/state architecture; WARP verified)** | **Define a stable render-pass order and resource-state transitions.** [Implemented](DX12RenderPassGraph.md): opaque world → terrain → water → effects → transparency → shroud → UI → postprocessing with strict stage ordering, explicit color/depth/shader/UAV resource bindings, alias/reverse-order rejection, native D3D12 transitions and UAV barriers, present/depth restoration, and safe resize/frame invalidation. [Win32/x64 WARP pass graph and GPU readback](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38824254425), [seven Windows build/audit jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38824254493) passed. **Actual W3D terrain/water/world/effect/shroud/UI draws still require R01/R04/R07/M01/B07; no gameplay parity is implied.** |
| **DX12-B06** | P0 | M | `DX12-B03` | Partial: preview | **Implement device loss, GPU timeouts, resize, and safe reset.** Alt-tab, fullscreen/window changes, device removed, suspend/resume and failed initialization recover or report an error without corrupting gameplay. |
| **DX12-B07** | P0 | XL | `DX12-B04`, `DX12-R01`, `DX12-R04` | Open | **Complete DX12 implementation of the neutral Renderer contract.** All methods used by gameplay have valid native DX12 behavior or an explicit unsupported error; no DX8-only compatibility dispatch in the selected backend. |
| **DX12-B08** | P0 | S | `DX12-B01` | Partial: preview | **Add honest coverage telemetry.** Count submitted and rejected draws plus objects excluded upstream; report feature category, fallback reason, upload failures and memory residency. |
| **DX12-B09** | P0 | M | `DX12-B04` | Partial: runtime HLSL | **Ship compiled shader bytecode and versioned permutations.** Normal DX12 builds do not require loose HLSL source files or runtime HLSL compilation; compiled PSOs and failure diagnostics are deterministic. |
| **DX12-B10** | P0 | M | `DX12-B02`, `DX12-B03`, `DX12-B04` | **Done (standalone DX12-only developer launch; verified)** | **Establish a real DX12-only launch checkpoint.** [Implemented](DX12OnlyLaunchCheckpoint.md): `W3DDX12LaunchCheckpoint` compiles `DX12Backend.cpp` exclusively, creates its application's primary HWND, presents native D3D12 frames, and exits or remains interactive without any D3D8 device/module. [Win32/x64 WARP, shader, PE import and source-contract CI](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38821817682) passed. This executable does not initialize game assets or guarantee gameplay parity; retail game builds still fail closed. |

**Source anchors:** [Renderer interface](../Core/Libraries/Source/WWVegas/WW3D2/Renderer.h) · [DX8 reference implementation](../Core/Libraries/Source/WWVegas/WW3D2/Backend/DX8Backend.cpp) · [DX12 presentation device](../Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12PresentationDevice.cpp) · [WW3D2 build configuration](../Core/Libraries/Source/WWVegas/WW3D2/CMakeLists.txt) · [Game display](../Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp).

### R. GPU resources, formats, state, and lifetime

| ID | Priority | Effort | Depends on | Current state | Scope and acceptance check |
|---|---|---|---|---|---|
| **DX12-R01** | P0 | L | `DX12-B04` | **Done (native geometry handles; Win32/x64 WARP verified)** | **Implement backend-owned vertex/index buffers and mapping.** [Implemented](DX12NativeGeometryBuffers.md): the exclusive DX12 backend now allocates static DEFAULT-heap and mapped dynamic UPLOAD-heap buffers, supports W3D byte-offset maps, DISCARD/NO_OVERWRITE versioning, index/vertex IA bindings, null-unbinding and per-frame fence-safe GPU/staging lifetimes; old W3D lock classes use the selected renderer rather than asserting Direct3D8. [Win32/x64 WARP GPU readback/tests](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38826379094) and [all seven Windows build/audit jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38826379107) passed. **R02** owns optimized dynamic rings, **R03** full vertex/index formats, **M01/B07** authoritative gameplay draw submission. |
| **DX12-R02** | P0 | L | `DX12-R01` | Open | **Migrate dynamic geometry rings, buffer offsets and upload synchronization.** Sorting, strips, particles and terrain tracks use safe transient upload pools with correct DISCARD/NO_OVERWRITE behavior and fences. |
| **DX12-R03** | P0 | M | `DX12-R01` | Partial: preview | **Support every W3D vertex layout and index representation in use.** FVF variants, normals, diffuse/specular colors, skin weights, multiple UV sets and 16/32-bit index paths are tested. |
| **DX12-R04** | P0 | XL | `DX12-B02` | Open | **Replace DX8/D3DX8-dependent texture loading.** DDS/BC-compressed and uncompressed W3D assets, mips, palette/format conversion, fallback images and resets load to DX12 without a DX8 texture or readback. |
| **DX12-R05** | P0 | L | `DX12-R04` | Partial: preview | **Implement stable texture handles, descriptors, and invalidation.** Create/bind/update/release all use DX12 resource identities; freed textures cannot leave stale SRVs or cache entries. |
| **DX12-R06** | P1 | M | `DX12-R04`, `DX12-R05` | Open | **Port samplers, mip selection, filtering, texture addressing and color space.** Wrap/clamp/border, point/bilinear/trilinear/anisotropic, sRGB and mip behavior match the DX8 reference on representative assets. |
| **DX12-R07** | P0 | L | `DX12-R04`, `DX12-B05` | Partial: swap chain | **Implement offscreen color, depth/stencil, render-to-texture and target switching.** Shadow, shroud and postprocess targets have correct formats, MSAA/resolve choices and transition barriers. |
| **DX12-R08** | P0 | L | `DX12-R04`, `DX12-R07` | Open | **Implement surface locks, dynamic updates, blits, copies, and readback.** Texture/surface CPU updates, read-modify-write operations, screenshot copies and texture-to-texture transfers work without D3D8 surfaces. |
| **DX12-R09** | P0 | L | `DX12-R01`, `DX12-R05` | Partial: synchronized preview | **Replace per-frame GPU stalls with resident streaming and fence-safe eviction.** No mandatory WaitForIdle every rendered frame; descriptors, staging allocations and resources stay alive through the final using fence. |
| **DX12-R10** | P1 | M | `DX12-R04`, `DX12-R05`, `DX12-R07` | Open | **Cover specialized texture resources and reset trackers.** Cube, volume, depth, procedural, render-target and dynamically updated textures preserve resource identity and intended lifetime. |

**Source anchors:** [Neutral buffer and texture handles](../Core/Libraries/Source/WWVegas/WW3D2/Renderer.h) · [DX8 texture implementation](../Core/Libraries/Source/WWVegas/WW3D2/texture.cpp) · [Legacy texture loader](../Core/Libraries/Source/WWVegas/WW3D2/textureloader.cpp) · [DX12 indexed mesh pass](../Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12IndexedMeshPass.cpp).

### M. W3D mesh submission, materials, and animation

| ID | Priority | Effort | Depends on | Current state | Scope and acceptance check |
|---|---|---|---|---|---|
| **DX12-M01** | P0 | XL | `DX12-B04`, `DX12-R01`, `DX12-R02` | Partial: preview mirror | **Replace mesh mirroring with authoritative DX12 W3D draw submission.** WW3D Flush, batches and polygon renderers submit to DX12 directly; geometry no longer depends on a parallel DX8 draw list. |
| **DX12-M02** | P0 | M | `DX12-M01`, `DX12-R03` | Partial: preview | **Complete rigid indexed geometry and shared-mesh instancing.** Full rigid models draw with correct transform, triangle ranges, mesh lifecycle, culling, vertex colors and mesh reuse. |
| **DX12-M03** | P0 | L | `DX12-M02`, `DX12-R06` | Partial: limited HLSL | **Implement legacy fixed-function texture combiners as modern shaders.** Every used W3D color/alpha combiner, texture factor, texcoord source/transform and filter stage has a tested shader or deliberate fallback. |
| **DX12-M04** | P0 | L | `DX12-M02`, `DX12-M03` | Partial: preview | **Support per-polygon textures, material categories and multi-pass meshes.** Different polygons and passes use their correct material/texture/UV state; the aircraft/building reference scenes have no diagnostic-color substitutions. |
| **DX12-M05** | P0 | L | `DX12-M03`, `DX12-B05` | Partial: alpha blend | **Complete raster, depth/stencil, alpha test, blend, cull and sorting-state variants.** All used blend equations, alpha refs, depth compare/write masks, color masks, cull orientations and stencil tests work. |
| **DX12-M06** | P0 | XL | `DX12-M02`, `DX12-R03` | Open | **Render skinned W3D meshes and animated bone deformations.** Infantry, vehicle attachments and articulated models use current interpolated bone poses without affecting gameplay animation timing. |
| **DX12-M07** | P1 | M | `DX12-M02` | Open | **Support aligned, oriented and camera-facing W3D geometry.** Billboards, aligned sprites, vegetation and facing effects rotate correctly with the gameplay camera. |
| **DX12-M08** | P0 | L | `DX12-M05`, `DX12-B05`, `DX12-R02` | Partial: deferred transparency | **Reproduce transparent polygon sorting and ordering.** Per-material and per-triangle sorting, depth interaction and alpha blending match DX8 in overlapping effects and model parts. |
| **DX12-M09** | P1 | M | `DX12-M02`, `DX12-M06` | Open | **Preserve W3D hierarchy, LOD and animation state changes.** Complex model hierarchies, LOD switches, model states, turret recoil and animation transitions render without missing sections. |
| **DX12-M10** | P1 | L | `DX12-M03`, `DX12-M04` | Open | **Port vertex lighting, fog, specular, ambient and material colors.** World lights, normals, vertex-material sources, distance fog and specular match a DX8 reference scene. |
| **DX12-M11** | P0 | L | `DX12-R02`, `DX12-M01` | Open | **Implement all W3D primitive drawing modes.** Triangle lists/strips, points/sprites, lines, ribbons and indexed dynamic draws work through neutral Renderer calls. |

**Source anchors:** [Zero Hour W3D mesh](../GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/mesh.cpp) · [W3D mesh batching](../Core/Libraries/Source/WWVegas/WW3D2/dx8renderer.cpp) · [DX8 polygon batches](../Core/Libraries/Source/WWVegas/WW3D2/dx8polygonrenderer.cpp) · [Transparent sorting](../Core/Libraries/Source/WWVegas/WW3D2/sortingrenderer.cpp) · [DX12 shader reference](../Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/W3DLegacyUnlit.hlsl).

### T. Terrain, shroud, water, and environment

| ID | Priority | Effort | Depends on | Current state | Scope and acceptance check |
|---|---|---|---|---|---|
| **DX12-T01** | P0 | L | `DX12-M01`, `DX12-R04` | Partial: preview | **Render full-resolution terrain natively.** All visible heightmap tiles use native DX12 buffers and original-resolution atlas resources; camera movement and cliffs have no holes or wrong UVs. |
| **DX12-T02** | P1 | L | `DX12-T01`, `DX12-M04`, `DX12-M05` | Open | **Reproduce terrain alpha blends and multi-pass overlays.** Two/three-way terrain blends, tile transitions, cliffs, texture layers and shroud edges match DX8 reference screenshots. |
| **DX12-T03** | P0 | M | `DX12-T01`, `DX12-M05` | Partial: preview | **Move standing and river water onto authoritative DX12 draws.** All map water surfaces remain visible, animated and correctly depth-tested without the DX8 water rendering code running. |
| **DX12-T04** | P1 | XL | `DX12-T03`, `DX12-R07` | Open | **Port special water modes and their render targets.** Sea/ocean grids, reflections, sparkles, noise, refraction-like passes and alternative water settings produce correct images. |
| **DX12-T05** | P1 | L | `DX12-T02`, `DX12-T03`, `DX12-M05` | Open | **Implement shoreline feathering and depth-dependent water appearance.** Coastlines have smooth alpha edges, no hard or rectangular water patches, and stable depth/transparency under camera movement. |
| **DX12-T06** | P0 | L | `DX12-T01`, `DX12-R07`, `DX12-M05` | Open | **Move gameplay shroud/fog-of-war render and update paths to DX12.** Single-player visibility, exploration, concealment and minimap shroud behave correctly without D3D8 resources. |
| **DX12-T07** | P0 | L | `DX12-T01`, `DX12-M11`, `DX12-R02` | Open | **Port roads, bibs, bridges and ground tracks.** Roads, bridges, pavement, building footprints and tracks render on and above terrain in both games. |
| **DX12-T08** | P1 | L | `DX12-R07`, `DX12-M01` | Open | **Port projected and render-target shadows.** Unit, building and terrain shadows are positioned, filtered and composited correctly. |
| **DX12-T09** | P1 | XL | `DX12-T08`, `DX12-M05` | Open | **Port volumetric/stencil shadows.** Existing Zero Hour and Generals stencil or volume shadow modes work without raw DX8 device calls. |
| **DX12-T10** | P1 | M | `DX12-T01`, `DX12-R08`, `DX12-M05` | Open | **Port decals, scorches, smudges, craters and terrain marking.** Weapon and ground-marking effects can update textures or draw overlays with correct depth and alpha. |
| **DX12-T11** | P1 | M | `DX12-T01`, `DX12-M07` | Open | **Finish trees, clouds, sky, terrain backdrop and environmental layers.** Maps contain all expected environmental objects and background layers across camera angles and graphical settings. |

**Source anchors:** [Heightmap renderer](../Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp) · [Water renderer](../Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp) · [Zero Hour shroud](../GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DShroud.cpp) · [Zero Hour roads](../GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DRoadBuffer.cpp) · [Zero Hour projected shadows](../GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp) · [Zero Hour volumetric shadows](../GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp).

### F. Particles, visual effects, and dynamic primitives

| ID | Priority | Effort | Depends on | Current state | Scope and acceptance check |
|---|---|---|---|---|---|
| **DX12-F01** | P0 | L | `DX12-M07`, `DX12-M11`, `DX12-R02` | Open | **Port sprite and model particle batching.** Smoke, dust, fire, explosions and emissions are visible, correctly timed and textured in DX12 gameplay. |
| **DX12-F02** | P1 | M | `DX12-M11`, `DX12-M08` | Open | **Port beams, lasers, trails, streaks and ropes.** Projectile trails, tracers, laser weapons and rope or line meshes render correctly at varied camera angles. |
| **DX12-F03** | P1 | M | `DX12-M03`, `DX12-M05` | Open | **Implement additive, emissive and special-effect blend programs.** Glow, fire and smoke use the intended alpha, color and brightness equations instead of disappearing or becoming solid. |
| **DX12-F04** | P1 | M | `DX12-M06`, `DX12-F01` | Open | **Port animated effect attachments, shatter and debris.** Bone-attached emitters, debris animations and model-state transitions update without duplicate emissions. |
| **DX12-F05** | P1 | M | `DX12-F01`, `DX12-T11` | Open | **Port snow, rain, weather and related point sprites.** Weather and snow sprites use DX12 point/quad primitives and retain original gameplay-independent timing. |
| **DX12-F06** | P1 | L | `DX12-M10`, `DX12-R07` | Open | **Port projectors, lighting effects and special scene overlays.** Projectors, scripted lighting changes and specialty overlays render without DX8-only shader or target accesses. |

**Source anchors:** [Particle manager](../Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DParticleSys.cpp) · [W3D streak renderer](../Core/Libraries/Source/WWVegas/WW3D2/streakRender.cpp) · [Terrain particles](../Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTerrainParticles.cpp) · [Snow renderer](../Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSnow.cpp) · [Decal system](../Core/Libraries/Source/WWVegas/WW3D2/decalsys.cpp).

### U. Menus, HUD, text, input, and video

| ID | Priority | Effort | Depends on | Current state | Scope and acceptance check |
|---|---|---|---|---|---|
| **DX12-U01** | P0 | L | `DX12-B05`, `DX12-R02`, `DX12-R04` | Open | **Add native 2D quad, color, image and clipping renderer.** Sprites, lines, panels and textured UI quads draw on the game's DX12 back buffer with correct alpha and scissor. |
| **DX12-U02** | P0 | L | `DX12-U01`, `DX12-R04` | Open | **Port fonts, glyph atlases and text layout.** Localized text, chat, tooltips, labels, hotkeys and outlined/drop-shadow fonts render legibly without DX8 glyph surfaces. |
| **DX12-U03** | P0 | L | `DX12-U01`, `DX12-U02`, `DX12-T06` | Open | **Port command bar, selection HUD, health indicators and minimap.** A human can select units, issue orders and read essential battle information in a DX12-only skirmish. |
| **DX12-U04** | P0 | L | `DX12-U01`, `DX12-U02` | Open | **Port title screen, options, mission UI and loading screens.** Menus, dialogs and loading transitions are visible and functional without DX8 initialization. |
| **DX12-U05** | P0 | M | `DX12-B03`, `DX12-U01` | Open | **Port hardware/software cursor, input focus and coordinate mapping.** Cursor hotspot, selection position, window focus and scaling agree with the rendered UI at multiple resolutions. |
| **DX12-U06** | P1 | L | `DX12-R07`, `DX12-R08`, `DX12-U01` | Open | **Replace DX8 shader-manager fullscreen effects and render-to-texture passes.** Fades, monochrome, crossfade, motion blur and other supported filters work without legacy pixel-shader tokens. |
| **DX12-U07** | P1 | M | `DX12-R08`, `DX12-U01` | Open | **Port video frames and cinematics to DX12 textures.** Briefings, intro/mission movies and any video-buffer overlays update and composite correctly. |
| **DX12-U08** | P1 | M | `DX12-B03`, `DX12-R08` | Open | **Port screenshots, frame capture and diagnostic overlays.** Screenshots, video capture interfaces and useful debug overlays work on the selected DX12 back buffer. |

**Source anchors:** [2D rendering](../GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp) · [Shader manager and fullscreen filters](../Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp) · [Text renderer](../Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplayString.cpp) · [UI window rendering](../Core/GameEngineDevice/Source/W3DDevice/GameClient/GUI/W3DGameWindow.cpp) · [Video buffer](../Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DVideoBuffer.cpp).

### V. Validation, first full-game launch, and DX8 retirement

| ID | Priority | Effort | Depends on | Current state | Scope and acceptance check |
|---|---|---|---|---|---|
| **DX12-V01** | P0 | M | `DX12-B08` | Open | **Build repeatable DX8-vs-DX12 screenshot and scene baselines.** Fixed maps, cameras, units, terrain, water, time-of-day and resolutions have comparable captured reference images and known tolerances. |
| **DX12-V02** | P0 | M | `DX12-M02`, `DX12-R07` | Partial: smoke tests | **Extend WARP pixel tests to all required render states.** Automated readback checks cover UVs, depth, blend, shadow/shroud formats, viewport, resizing and missing-resource behavior. |
| **DX12-V03** | P0 | XL | `DX12-B10`, `DX12-M01`, `DX12-T01`, `DX12-T06`, `DX12-U03`, `DX12-U04` | Open | **Reach the first playable DX12-only single-player match.** Zero Hour boots, menus work, skirmish starts, units respond, terrain/shroud/HUD are usable, and one complete match runs with no DX8 device. |
| **DX12-V04** | P0 | M | `DX12-B02`, `DX12-V02` | Partial: dual CI | **Build and test true DX12-only binaries for both games in CI.** Generals and Zero Hour Win32 DX12-only executables build from clean checkout and run deterministic WARP smoke tests. |
| **DX12-V05** | P1 | M | `DX12-B06`, `DX12-V04` | Open | **Verify physical GPU and OS compatibility.** Validated on representative AMD, Intel, NVIDIA and WARP adapters, including resize, minimize, reset and device-loss cases. |
| **DX12-V06** | P1 | L | `DX12-R09`, `DX12-V03` | Open | **Benchmark memory, GPU usage, stutter and stability.** Long single-player matches have bounded GPU memory, no per-frame mandatory GPU idle, no resource leaks and acceptable frame pacing. |
| **DX12-V07** | P0 | M | `DX12-B01`, `DX12-V03` | Open | **Enforce no Direct3D8 usage in the selected DX12 runtime.** The DX12-only executable neither creates a D3D8 device nor depends on D3D8 runtime rendering; automated import/call-path checks guard regressions. |
| **DX12-V08** | P1 | L | `DX12-V07`, `DX12-T02`, `DX12-T04`, `DX12-T05`, `DX12-T07`, `DX12-T08`, `DX12-T09`, `DX12-T10`, `DX12-T11`, `DX12-F01`, `DX12-F02`, `DX12-F03`, `DX12-F04`, `DX12-F05`, `DX12-F06`, `DX12-U06`, `DX12-U07`, `DX12-U08` | Open | **Retire the DX8 backend after feature parity.** The same required visuals and functional flows run through DX12; DX8 fallback can be disabled and DX8 implementation/code deleted safely. |
| **DX12-V09** | P0 | M | `DX12-B09`, `DX12-V04` | Open | **Package the DX12 executable as an installable artifact.** Shader binaries, assets, configuration and dependencies are included; no manual HLSL sidecar copy is needed. |
| **DX12-V10** | P1 | L | `DX12-V03`, `DX12-V04` | Open | **Regression-test campaign, save/load, replay and multiplayer compatibility.** Campaign and skirmish sequences, loading and saving, replays, and multiplayer presentation preserve gameplay behavior and deterministic simulation. |
| **DX12-V11** | P0 | M | `DX12-V03` | Open | **Protect 30 Hz simulation with uncapped DX12 presentation.** 30/60/144/240 Hz rendering never changes simulation tick counts, replay determinism, input response or client-side interpolation. |

**Source anchors:** [Boundary audit](../scripts/check_w3d_dx8_boundaries.py) · [Windows validation workflow](../.github/workflows/w3d-renderer-validation.yml) · [DX12 WARP validation](../.github/workflows/dx12-shader-validation.yml) · [Existing modernization history](../Documentation/W3DRendererModernization.md) · [Simulation presentation test notes](../Documentation/VisualPresentationSmoothing.md) · [DX8 SDK dependency](../cmake/dx8.cmake).

## Milestone acceptance gates

| Milestone | Observable result | Stop condition |
|---|---|---|
| Preview baseline (existing) | Secondary DX12 preview draws meshes/terrain/water; WARP tests pass | Not evidence of DX12-only support |
| First DX12-only developer render (`B10`) | Standalone DX12-only developer application's primary HWND presents native DX12 frames; Win32/x64 PE imports exclude D3D8 | No native present, failed PE audit, or DX8 module loaded |
| Playable DX12-only single-player (`V03`) | Start, play and finish a Generals/Zero Hour skirmish with menus, HUD, terrain, shroud and unit controls | Any mandatory render subsystem still requires an IDirect3D8 device |
| Complete DX8 feature parity | Campaign, multiplayer presentation, video/menus, shadows, effects, water and graphics settings work | Visible or functional effects are missing in supported configurations |
| DX8 removed (`V07`, `V08`) | DX12 build links, initializes, runs and presents independently of DX8 SDK, d3d8.dll and DX8 backend; tests pass | Any imported/reachable mandatory D3D8 render dependency remains |

## Regression matrix

- Both executables: **Generals** and **Zero Hour** Win32; standard DX8-reference build and selected DX12-only build until retirement.
- Modes: menu, campaign mission, single-player skirmish with several factions, save/reload, replay, multiplayer compatibility, minimap/shroud, graphics settings, full-screen/windowed resizing.
- Maps: desert/coast with terrain blends and water; dense urban with roads/bridges/shadows; trees/infantry/particles; special sea/water and shroud states.
- Rendering: opaque, translucent, alpha-test, depth and stencil, water layers, UI fonts, postprocess/video; frame capture against fixed DX8 camera/scene.
- GPUs: CI WARP and developer hardware across major vendors; include device removed, alt-tab/minimize, resize, 30/60/144/240 Hz simulation decoupling checks.
- Release conditions: shader bytes packaged; neither game regresses; no mandatory DX8 device initialization; explicit errors rather than invisible dropped draws.

## Sources / audit limits

- [Historical renderer-modernization notes](W3DRendererModernization.md) describe earlier completed interface migration. Portions of their old DX12-preview summaries are stale (for example, 12 meshes and untextured rendering). This backlog is the current planning source.
- [`scripts/check_w3d_dx8_boundaries.py`](../scripts/check_w3d_dx8_boundaries.py) **only** bans direct DX8 calls in explicitly migrated files and patterns. It is **not** a whole-repository check proving DX8 independence.
- [`Backend/DX8Backend.cpp`](../Core/Libraries/Source/WWVegas/WW3D2/Backend/DX8Backend.cpp) still implements `Renderer` using DX8Wrapper. [WW3D2 CMake](../Core/Libraries/Source/WWVegas/WW3D2/CMakeLists.txt) still compiles that backend; the DX12 path is controlled by `W3D_ENABLE_DX12_GAMEPLAY_PREVIEW`.
- [`DX12GameplayPreview.cpp`](../Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12GameplayPreview.cpp) owns the secondary window and retains a frame-ending GPU wait; the [texture loader](../Core/Libraries/Source/WWVegas/WW3D2/textureloader.cpp), [shader manager](../Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp), [water renderer](../Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp) and [shadow systems](../GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp) still contain native DX8 behavior.
- Validation: [W3D renderer Win32 matrix](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38803009784) and [shader/WARP validation](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38803009756), both green at the audited baseline.

**Change control:** Keep existing DX8 default stable until the DX12-only branch passes the corresponding milestone; do not merge incomplete replacement behind the working DX8 path without an explicit opt-in.
