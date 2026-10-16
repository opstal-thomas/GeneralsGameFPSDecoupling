# DX12-M01 / DX12-B07 — authoritative W3D draw integration

**Status:** in progress, native draw milestone implemented (2026-10-16).
**Branch:** `feature/w3d-renderer-backend-foundation`.

This work replaces preview-only draw submission with native Direct3D 12
commands from the original W3D gameplay Renderer contract. It does **not**
yet meet the full M01/B07 acceptance gates, and the selected DX12 game
build remains intentionally fail-closed outside the developer checkpoint.

## Implemented

- `Renderer::Draw_Triangles` and `Renderer::Draw_Triangle_Strip` record
  `DrawIndexedInstanced` on the selected primary DX12 presentation command
  list. Input indices and vertices come from the original native W3D R01/R02
  resource bindings, **not** the opt-in DX8 gameplay preview's reconstructed
  meshes. Both R16/R32 index formats and decoded R03 FVF input layouts are
  exercised.
- Backend-private `DX12W3DNativeDraw` creates and caches actual D3D12
  graphics PSOs and root signatures, compiles built-in SM5.1 shaders,
  binds the R05 two-stage texture descriptor tables, and attaches to the
  selected R07 primary/offscreen render targets. It supports indexed
  triangles/strips, colored rigid layouts, up to two 2D UV sets, shader
  alpha-test clipping, basic depth/cull/blend and unlit material values.
- W3D ShaderClass bitfields translate through a typed adapter to the
  neutral renderer's blend, depth and cull state. Face-culling direction
  honors W3D's clockwise default and reflection inversion. The
  TextureBaseClass bridge passes the existing backend-neutral render
  texture handle. VertexMaterialClass diffuse/opacity enters per-draw
  shader constants, including material alpha overrides.
- The original CPU WW3D mesh categories/batch flush, polygon renderers
  and mesh camera are now dispatched through the neutral Renderer
  methods instead of direct DX8Wrapper buffer, shader, texture and
  triangle calls. The old DX8 renderer remains the reference selection.
  The CPU mesh queue is still named `DX8MeshRenderer`; its selected
  DX12 dispatch is through Renderer, not a DX8 GPU device.
- Drawing rejects out-of-bounds index/vertex ranges, missing bindings,
  invalid layouts and unsupported depth-only color passes.
  Per-frame root/PSO ownership and resource residency follow the
  previously verified presentation submission-fence lifecycle.

## Verified

[DX12 W3D Shader Validation run 38966299246](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38966299246)
passed shader compilation, selected-Renderer source contracts, and
Win32/x64 native D3D12 WARP tests. The new
`W3D_DX12_WARP_Authoritative_Indexed_Draw` draws packed-D3DCOLOR raw
W3D R16 triangles and R32 strips using the **selected native W3D path**,
checks out-of-range rejection, copies back actual GPU pixels and
verifies their color. No DX8 texture or draw mirror participates.

[Windows game validation run 38966299293](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38966299293)
**passed all seven jobs**: the DX8 dependency audit, normal Generals and
Zero Hour builds/tests, their DX12-preview builds/tests, and both
DX12-exclusive bootstrap builds/tests. This verifies the source-level
compatibility and native draw integration, **not** playable DX12-only gameplay.

## Not yet implemented — keep M01/B07 open

- `Renderer::Capture_Sorting_State`, `Restore_Sorting_State`,
  `Release_Sorting_State`, sorted deferred transparency/light capture
  and all ordering/transform parity. Sorting source geometry was
  integrated by R02, but this is **not** completed sorted draw execution.
- Full material/lighting, fog, texture combiners, samplers and all
  shader permutations (M02/M03/R06). The initial shader is a deliberately
  restricted unlit/single-pass baseline. W3D RHW transform semantics,
  additional FVF options, procedural materials and special geometry are
  not asserted to be gameplay-equivalent.
- Complete terrain/water, shroud, particles, HUD/menu/UI, immediate
  primitives and surface blit/readback effects (T/U/F/R08).
- Remaining DX12Backend `ReportPending` methods, error reporting
  and the B07 full neutral Renderer parity audit.
- Launch and play an actual DX12-only Generals/Zero Hour single-player
  skirmish without the developer checkpoint (V03, later after the above).

**Next implementation gate:** correctly implement sorted state and
transparency submission from the original W3D queues, then validate
real model rendering with terrain/UI in a DX12-only test map.
