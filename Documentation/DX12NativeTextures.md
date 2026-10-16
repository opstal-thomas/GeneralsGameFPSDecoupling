# DX12-R04 — Native W3D texture loading and CPU image conversion

**Scope:** Direct3D 12 texture resource loading and CPU pixel data ingestion.
The DX12 backend now implements the existing neutral `Renderer` texture
creation, mip mapping, update, refcount, format-query and image-surface
interfaces. It **does not** make Generals / Zero Hour gameplay playable on
DX12; R05/R06/R07 and authoritative draw submission remain separate gates.

## What changed

`DX12W3DTextures.{h,cpp}` creates backend-owned 2D, cube and 3D texture
handles with CPU-backed mip/subresource storage. The existing W3D
`TextureLoader` continues parsing `DDSFileClass` and `Targa` data through
its original asset/FileFactory and CPU `BitmapHandlerClass` routines; the
decoded rows now flow through real `Renderer::Map_Texture_2D`,
`Map_Texture_Cube`, `Map_Texture_Volume`, and matching unmap functions.
The old direct D3DX8 include dependencies were removed from the W3D
texture, texture-loader and surface source files where no D3DX API is used.

The backend supports:

| W3D source format | DX12 representation |
| --- | --- |
| DXT1 | `DXGI_FORMAT_BC1_UNORM` (8-byte blocks) |
| DXT2 / DXT3 | `DXGI_FORMAT_BC2_UNORM` (16-byte blocks) |
| DXT4 / DXT5 | `DXGI_FORMAT_BC3_UNORM` (16-byte blocks) |
| 24/32-bit RGB/XRGB/ARGB | GPU `DXGI_FORMAT_B8G8R8A8_UNORM` with matching color-channel/alpha conversion |
| R5G6B5, A1/X1R5G5B5, A4/X4R4G4B4, R3G3B2, A8R3G3B2 | Correct CPU unpacking and BGRA8 upload |
| A8, L8, A8L8, A4L4 and legacy bump data | CPU normalization into BGRA8; final material/sampler interpretation is R06 |
| P8 / A8P8 | Explicit CPU palette expansion using `ConvertPaletteToBGRA8`; do not submit an unconverted palette-index texture as a GPU color image |

Source 2D mips retain the source's **tight byte row pitch**, including
compressed block rows. After the W3D loader unmaps a level, uploading is
deferred until a native DX12 frame can record `CopyTextureRegion`. All
subresources, including all six cube faces or depth slices of a volume
texture, are uploaded with `GetCopyableFootprints` and the API-mandated
staging row alignment. Native resources are transitioned from
`COPY_DEST` to pixel/nonpixel `SHADER_RESOURCE` through the B05 pass
tracker. Resource state does not rely on a guessed `COMMON` transition.

**GPU ownership and resets:** Texture references are thread-safe, and CPU
mip shadows remain available when the swap chain/device is torn down.
When a new DX12 device is selected, the next use creates fresh GPU storage
and reuploads the mip data. Updating an existing resource creates a *new*
GPU generation rather than overwriting a resource currently referenced by
a recorded frame. R01's presentation fence keeps prior generations and
their UPLOAD staging alive through the last using submission.

## Image surfaces, file decoding and fallback

`DX12W3DSurfaces.{h,cpp}` replaces DX8 image-surface allocations/locks
with CPU buffers and provides `Renderer::Create_Surface`,
`Lock_Surface`, `Unlock_Surface`, refcount, copy and image blit
support. `Renderer::Create_Texture_From_Surface` normalizes an image
surface to BGRA8 and generates a full or requested mip chain using a
four-sample CPU box downscale before native upload.

`Renderer::Load_Surface` supports standalone filesystem TGA files,
including 24/32-bit truecolor, grayscale, 8-bit indexed palettes and
raw/RLE encoding, with correct image-origin handling. For assets located
inside W3D archives, **the existing FileFactory-based TextureLoader** is
the supported DDS/TGA image ingestion path, not this direct filesystem
surface helper.

On the exclusive DX12 build, `MissingTexture::_Init` now allocates the
fallback through the neutral Renderer, writes a predictable magenta/black
checker into every mip, and `_Create_Missing_Surface` creates a native
CPU image surface. The DX8 branch keeps its original behavior.

## Native validation

The new `W3DDX12TextureSmoke` is a real Windows WARP D3D12 test. It
checks uncompressed and BC1/BC3 texture creation, raw compressed mip
pitch/footprints, cubemap faces, volume slice pitches, palette conversion,
indexed RLE TGA loading, CPU surface copy and box mip generation, and
GPU readback of the actual uploaded texture bytes. It also checks
same-frame texture rewrites, early release, fence retirement, native null
resource recovery and device recreation on both Win32 and x64.
`scripts/tests/test_dx12_texture_loading.py` checks the public W3D
format mapping, native backend implementation and test coverage.
These tests run alongside previous WARP smoke tests and both Generals
and Zero Hour Windows build configurations.

**DX12-specific CI passed:** [Win32 and x64 R04 WARP upload/readback,
game-owned HWND Renderer integration, shader compilation and source
contracts](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38829454476).
The [full seven-job Generals and Zero Hour Windows
build/dependency-audit matrix](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38829454383)
also passed, including normal DX8, DX12 preview and exclusive DX12 bootstrap
configurations.

## Remaining boundaries

- **R05:** Stable shader-visible SRV descriptor allocations, texture stage
  binding, invalidation and draw-time descriptor cache lifetime. The R04
  bind path ensures GPU content is ready but does **not** yet populate
  authoritative W3D material descriptors.
- **R06:** Exact filtering/anisotropy, sRGB, alpha-premultiplication/bump
  semantics and material sampling parity.
- **R07:** Render targets, depth/stencil and offscreen texture attachment
  creation. R04 intentionally rejects `renderTarget=true` instead of
  supplying a non-renderable resource.
- **R08:** GPU surface-level acquisition, texture-to-surface transfers,
  screen readback and full image blit/copy parity. R04 handles ordinary CPU
  image surfaces, but not every legacy DX8 surface operation.
- **R09/M01/B07:** Efficient streaming/eviction, fully native W3D draws and
  actual DX12 gameplay.
- The legacy W3D `SurfaceClass` and `TextureClass` headers still expose
  D3D8-shaped compatibility pointers in selected constructors. Those
  call sites must be fully replaced before the **V07** no-DX8
  process/import audit and full single-player renderer cutover.

The DX8 game binary continues using its existing renderer unchanged.
