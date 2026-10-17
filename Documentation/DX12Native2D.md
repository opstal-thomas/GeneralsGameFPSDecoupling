# DX12-U01 — first native W3D Render2D path

**Status:** Partial, 2026-10-17. This milestone does **not** make DX12-only
Generals/Zero Hour gameplay, menus or HUD functional or validated.

## Scope delivered

The original `Render2DClass::Render()` implementations in both
`Generals/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp` and
`GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp` now
have an exclusive `W3D_RENDER_BACKEND_DX12` submission path:

1. Preserve the caller's normalized 2D vertex coordinates, packed ARGB
   colors, UV0, depth, triangle indices, shader and material selection.
2. Save/restore WORLD, VIEW and PROJECTION transforms. Render sprites in
   identity WVP with a full render-target viewport, so they bypass the
   current 3D camera without corrupting later world draws.
3. Allocate existing W3D dynamic vertex/index accessors with
   `BUFFER_TYPE_DYNAMIC_SORTING`, not D3D8's dynamic buffer storage.
   R02 immediately snapshots their CPU bytes into fence-retained DX12
   transient geometry; original vertices and font atlas quads share the
   authoritative M01 `DrawIndexedInstanced` renderer.
4. Apply `Renderer::Set_Texture` to inherit lazy texture initialization,
   per-material filter states, and R05/R06 texture/sampler descriptors.
   Unbind stale secondary texture stages.
5. Apply the W3D shader's source/destination alpha, depth, cull and
   texture-combiner settings and disable vertex lighting for prelit 2D
   colors. DX12's native PSO evaluates blending; R07 binds the primary
   color/depth attachments.
6. Preserve the existing DX8 path in the other compile-time selection.
   It still uses the legacy dynamic DX8 accessor and original state cache.

**Current grayscale limitation:** the DX12 path uses the original
no-DOT3 darkening fallback for disabled assets. It is not yet a faithful
grayscale/luminance-conversion shader; the specialized DOT3 fixed-function
equation is a subsequent M03/U01 task.

## Validation and remaining gates

- `scripts/tests/test_dx12_render2d.py` asserts both game variants use
  CPU sorting geometry and the native Renderer draw contract and have no
  DX8Wrapper calls in their selected DX12 2D render branch.
- M01/R02/R04/R05 WARP tests cover the underlying FVF, texture upload,
  sampler, alpha blend, and GPU-fence-safe draw path, but cannot execute
  `Render2DClass::Render` in an actual DX12-only game scene.
- The first Windows build exposed an invalid `static_cast` of the legacy
  formatted vertex pointer; both variants now use `reinterpret_cast`.
  **Current validation:** [Windows renderer CI](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/39024323620).
  Check the run result rather than assuming success.

**U01 acceptance remains open** for software UI clip/scissor integration,
line and primitive variants, multi-stage/animated widgets, true grayscale,
font texture/update edge cases, transformed screen quads, and visually
verified menu/HUD rendering. R08 GPU image copies/readback and
T01/T03 terrain/water remain separate P0 dependencies before the V03
DX12-only single-player skirmish gate can be attempted.
