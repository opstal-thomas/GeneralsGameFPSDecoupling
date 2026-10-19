# DX12-U02 — native GDI glyph atlas upload (partial)

**Status: native font atlas upload implemented, in-game UI/font parity open.**

## Confirmed DX12-only bug

The original `Render2DSentenceClass::Build_Textures()` first rasterizes
text into a CPU `SurfaceClass` with `WW3D_FORMAT_A4R4G4B4`. It then asks
a `TextureClass` for a writable `Get_Surface_Level()` and copies glyph
pixels there. On exclusive DX12,
`Renderer::Acquire_Texture_Surface_Level()` is intentionally routed to the
R07 render-target manager, which rejects ordinary sampled textures. The
legacy glyph upload could never populate the sampled glyph atlas. U01
primitive validation did not exercise font generation.

## Native fix

With `W3D_RENDER_BACKEND_DX12`, the sentence renderer now constructs
`TextureClass(curr_surface, MIP_LEVELS_1)` from the **populated GDI CPU
surface**. The existing backend `Renderer::Create_Texture_From_Surface()`
uses `DX12W3DSurfaces::ToTexture()` to convert packed A4R4G4B4 alpha into
BGRA8 and then upload to the shader-visible texture stage. The original
`Render2DClass` still draws all sentence glyphs, Unicode characters,
hotkey overlays and text/drop-shadow quads. It does not require a D3D8
staging surface or an R07 render-target view.

A failed upload keeps its pending CPU surface for retry and suppresses
rendering of the sentence while one or more pages are missing, rather than
silently emitting solid rectangles. Successful glyph surfaces release their
references exactly once. The original DX8 branch retains its copy path.

## Checks

- `scripts/tests/test_dx12_font_atlas.py` covers sentence ownership,
  screen/widget text consumers, backend constraints, retry semantics and
  packed alpha conversion.
- `DX12TextureSmoke.cpp` uploads packed 2-by-2 A4R4G4B4 glyph coverage to
  actual WARP GPU memory and compares readback to BGRA8 pixels for opaque,
  half-transparent and fully transparent coverage.
- The first successful native glyph atlas logs
  `U02: first native glyph atlas converted and bound` in
  `%TEMP%\\GeneralsDX12Startup.log`. A retry logs
  `U02: native glyph atlas upload unavailable; will retry`.

## Outstanding acceptance

Confirm original game startup and a first native Present (T01/V03),
localized text and glyph metrics, visible menus/options (U04), HUD/minimap
(U03), input/cursor alignment (U05) and matching DX8-vs-DX12 screenshots
at multiple resolutions. **This is an incremental U02 fix, not a claim
that the entire UI already works.**
