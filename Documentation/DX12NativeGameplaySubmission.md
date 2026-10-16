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
- The M01 sorting-state bridge now snapshots original WW3D shader,
  material, texture, vertex/index and transform state in the typed CPU
  adapter. Deferred nodes retain the original W3D owners and renderer
  bindings; restoration reconstructs the R02 dynamic/sorting slices,
  world/view transforms, shader state and base-vertex index. Indexed
  replay then uses the authoritative DX12 draw path instead of a
  separate DX8 draw. Sorting matrix multiplication and homogeneous
  transformed-center calculations match the historic D3DX row-vector
  convention. The CPU sorting queue still chooses draw order.
- The B07 native draw PSO now has distinct source/destination blend
  factors, alpha blend equations, additive/reverse-subtract operations,
  independent RGB/alpha write masks and blend enable. These contribute
  to the immutable PSO cache key, avoiding state bleed between adjacent
  transparent draws. The WARP test records a second masked draw and
  verifies the previous pixel is preserved on GPU readback. Existing
  DX8 dependency audit and source-interface guardrails remain in force.

## Verified

[DX12 W3D Shader Validation run 38966299246](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38966299246)
passed shader compilation, selected-Renderer source contracts, and
Win32/x64 native D3D12 WARP tests. The new
`W3D_DX12_WARP_Authoritative_Indexed_Draw` draws packed-D3DCOLOR raw
W3D R16 triangles and R32 strips using the **selected native W3D path**,
checks out-of-range rejection, copies back actual GPU pixels and
verifies their color. No DX8 texture or draw mirror participates.

[Sorting/blending WARP validation run 38970703645](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38970703645)
**passed** Win32/x64 WARP GPU tests, shader compilation, source contract
checks, homogeneous sorting matrix smoke assertions and the new masked
reverse-subtract draw / native PSO cache readback assertion.

[Windows game validation run 38970703978](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38970703978)
**passed all seven jobs**: the DX8 dependency inventory/boundary audit,
normal Generals and Zero Hour builds/tests, both DX12-preview builds/tests,
and both DX12-exclusive bootstrap builds/tests. These tests prove the
native bindings compile and link with the games, but **do not** establish
rendering parity in an actual skirmish.

## M01/B07 native vertex-lighting and matrix milestone (2026-10-16)

- Original `LightEnvironmentClass` values are translated in the typed
  W3D mesh adapter into `RenderLightingEnvironmentDescription`, without
  linking game-only light or D3D8 objects from the standalone DX12 backend.
  The environment supplies the scene's equivalent ambient and at most four
  attenuated directional contributors. `Set_Ambient`, `Set_Vertex_Lighting`
  and `Set_Vertex_Color_Sources` now drive real native shader state.
- Selected `VertexMaterialClass` diffuse/ambient/emissive/opacity values
  and original material/vertex-color 1/vertex-color 2 source selectors
  reach the native vertex shader. Original packed D3DCOLOR is BGRA-swizzled
  and vertex NORMAL/COLOR1 layouts are decoded by the established R03 path.
  The first lit shader evaluates ambient/emissive plus Lambert directional
  diffuse, using the correct inverse-transpose of the world transform for
  normals even when model scale differs by axis.
- Since four lights plus normal/material state exceed D3D12's 64-DWORD
  root-constant limit, every native W3D draw writes its immutable lighting
  constants into a 256-byte-aligned CBV slice from the R02 transient
  upload pool. The D3D12 frame retains each page until its submission
  fence completes; deferred alpha draws do not alias prior constants.
- `RenderStateStruct` now includes a backend-neutral lighting-value
  snapshot copied along with deferred W3D sorting state. Sorted replay
  restores the actual submitting object's ambient and directional lights,
  not the state left by another opaque mesh.
- The native world/view/projection path transposes Westwood's column-vector
  matrices exactly as the DX8 adapter's `To_D3DMATRIX` does before
  multiplying World * View * Projection. The raw transform-elements
  getter also returns the DX8 row-vector representation.

**Verified:** [WARP and contract validation 38972824571](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38972824571)
passed native Win32/x64 GPU tests, including drawing an FVF
XYZ/NORMAL/DIFFUSE red-vertex triangle as green under a material-sourced
green light, then black when the light faces away. Existing triangle/strip,
sorting and alpha-mask regression checks also passed.
[Windows renderer validation 38972824650](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38972824650)
passed all **seven** jobs: DX8 dependency inventory, Generals and Zero Hour
normal builds, both preview builds and both DX12-exclusive bootstrap builds.

**Still incomplete:** DX8-style point and spot lighting require correct
position/attenuation parity rather than this equivalent-directional
baseline. Specular material/light response, texture-stage combiners,
samplers/UV transformations, vertex skinning, fog, RHW and remaining
B07 renderer methods still need implementation. No playable DX12-only
single-player skirmish or full in-game parity capture has been verified.

## Not yet implemented — keep M01/B07 open

- End-to-end in-game sorted-alpha visual verification and parity.
  CPU state capture/restore, dynamic buffers and basic native replay are
  implemented, but scene lighting from captured lights is **not** native
  yet; the M03 lighting path must preserve mesh/environment overrides.
  This stage has shader/source/WARP-level tests, not a rendered
  Generals/Zero Hour gameplay comparison. Transparency ordering edge
  cases and specialized material passes also remain to be validated.
- Complete material/fixed-function parity, exact point/spot-light
  attenuation and specular reflection, fog, texture combiners, samplers
  and all shader permutations (M02/M03/R06). Directional/ambient
  vertex lighting and color sources are now implemented, but the
  native shader remains single-pass and W3D RHW, skinned layouts,
  procedural materials and special geometry are not gameplay-equivalent.
- Complete terrain/water, shroud, particles, HUD/menu/UI, immediate
  primitives and surface blit/readback effects (T/U/F/R08).
- Remaining DX12Backend `ReportPending` methods, error reporting
  and the B07 full neutral Renderer parity audit.
- Launch and play an actual DX12-only Generals/Zero Hour single-player
  skirmish without the developer checkpoint (V03, later after the above).

**Next implementation gate:** extend the first native lit shader to DX8
point/spot lights, vertex specular and fixed-function texture/color
combiners (M03/R06), then validate deferred sorted-alpha and opaque
meshes against DX8 in a real single-player map. Complete B07's remaining
Renderer methods and the T01/U01/R08 terrain/UI/copy paths before
enabling the DX12-only gameplay gate.
