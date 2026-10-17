# DX12-U01 — native W3D 2D sprites, primitives and clipping

**Implementation status: Complete (2026-10-17), validated with standalone
DX12 WARP pixels and source-contract checks.** Full in-game visual parity
cannot be signed off until the DX12-only playable V03 gate exists.

## Implemented for both Generals and Zero Hour

The original `Render2DClass::Render()` selects exclusive
`W3D_RENDER_BACKEND_DX12` geometry submission without D3D8 draw/resource
calls. The existing DX8 implementation is kept intact in the other
compile-time branch.

- **Native 2D primitive batches.** Existing `Add_Quad`, `Add_Line`,
  `Add_Tri`, `Add_Outline`, `Add_Rect`, gradients and text glyph quads
  emit actual indexed triangles from their original W3D normalized
  coordinates, BGRA-packed vertex colors, UV0 and Z values.
  `BUFFER_TYPE_DYNAMIC_SORTING` feeds R02's fence-retained transient
  buffer path and authoritative M01 `DrawIndexedInstanced`; no DX8
  `DynamicVBAccessClass` GPU buffer is allocated by selected DX12.
- **Texture/image state and alpha.** Original `TextureClass::Apply`
  performs deferred load and W3D min/mag/mip and UV addressing; R05/R06
  descriptors bind it on the selected native shader, not the secondary
  DX8-backed gameplay preview. R06 W3D color/alpha combiners and M05
  blending support opaque, alpha and additive image/widget modes. The
  second texture stage is explicitly unbound to avoid stale world
  textures bleeding into interface panels.
- **Camera-independent viewport.** The prior viewport and three W3D
  transform matrices are preserved across a 2D draw; the native 2D
  pass uses identity WORLD/VIEW/PROJECTION and full target dimensions,
  then restores viewport/transform state before returning to the scene.
- **Native clipping.** `Render2DClass::Set_Clip_Rect` and
  `Enable_Clip` expose a per-batch screen-space clip. Its coordinates
  are transformed through the *same* normalized/bias mapping as the
  original quad geometry. The DX12 primary HWND backend intersects
  this physical-pixel rect with the active viewport to set a hardware
  rasterizer scissor; disabled clip restores the camera viewport
  scissor. The inherited `Render2DTextClass` APIs update this same
  clip, so partially overlapping glyphs emit and are cut accurately
  in hardware rather than discarded entirely.
- **Disabled-image grayscale.** The native pixel shader performs
  luminance conversion after sampling/combiners and preserves alpha.
  The state uses an existing per-draw constant-buffer channel,
  avoiding a new PSO cache key or DX8 shader-token dependency. It is
  disabled immediately after each 2D draw, so opaque world materials
  are unaffected.
- **DX8 parity boundary.** The DX8 backend still uses its original
  dynamic buffer and render path. New API methods are neutral
  declarations; DX8 no-ops only apply to DX12-specific scissor/
  grayscale calls that the DX8-selected `Render2DClass` never makes.

## Regression validation

- `scripts/tests/test_dx12_render2d.py` enforces geometry ownership,
  texture binding, shader selection, clip placement, camera restoration,
  grayscale state, no DX8Wrapper calls inside selected 2D branch and
  the WARP test's expected coverage. Both central Windows CI
  workflows explicitly run these assertions.
- `DX12NativeDrawSmoke.cpp` exercises the real authoritative renderer
  with WARP GPU readback for scissor exclusion, scissor inclusion and
  luma-converted red output at the same pixel. Existing WARP tests also
  cover two textured UV stages, sampler modes, alpha blending and
  opaque+transparent compositing.
- [DX12 WARP shader validation, including U01 pixel tests, passed](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/39026322890).
- [Generals and Zero Hour Win32 build/audit matrix](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/39026322926) validates that both renderer selections compile.

## Separate dependencies — not U01 blockers

- **U02:** full font atlas format/update coverage, glyph layout and
  localized text; U01 merely renders their existing 2D triangle geometry.
- **U03/U04/U05:** HUD widget wiring, menus, cursors and input.
- **U06:** specialized DX8 legacy shader tokens, DOT3 tint parity,
  fullscreen filters and `Draw_Transformed_Quad`.
- **R08:** GPU render-target surface readback/blit/update.
- **T01/T03/T06:** terrain, water, and gameplay shroud.
- **V03:** a playable DX12-only single-player skirmish and matched
  DX8-vs-DX12 screenshot comparisons. This remains intentionally gated;
  a source or WARP pass is not an in-game parity proof.
