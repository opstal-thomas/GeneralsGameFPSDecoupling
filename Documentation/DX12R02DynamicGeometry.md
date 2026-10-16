# DX12-R02 — fence-safe W3D transient geometry and producer integration

**Status: Done (2026-10-16).** R02 migrates native dynamic geometry
allocation and the legacy W3D geometry producers to the renderer-neutral
DX12 resource/binding seam. The separate M01/B07 authoritative gameplay
draw pipeline is **not** part of R02.

## Device-owned dynamic upload rings

- Per-DX12-presentation UPLOAD pages, normally 1 MiB, with aligned 16-byte
  suballocations shared by native dynamic vertex/index resources and borrowed
  CPU-only sorting geometry. Larger allocations receive appropriately sized
  pages. There is **no hard per-frame mesh or page limit**.
- Upload pages are append-only within a recording. DISCARD and
  NO_OVERWRITE each generate immutable GPU slices; recorded draws are not
  corrupted by later writes or CPU-side buffer destruction.
- `EndFrame` retires used pages using that submission's *actual signalled
  fence*. Recycling requires a completed fence; a new recording never
  infers safety from the last-submitted counter alone.
- Idle upload-page cache has a soft 64 MiB target. Pages referenced by the
  current or incomplete submissions are not reclaimed, and shutdown clears
  all pages before device recreation.
- Binding copies only the requested shadow range for dynamic handles.
  IA vertex/index GPU addresses include the physical upload suballocation
  offset. Dynamic handles rebound in a later frame resnapshot their CPU
  shadows even when no new map/unmap was performed.

## W3D producer integration

- DynamicVBAccessClass and DynamicIBAccessClass retain their original
  DX8/DX12-neutral binding shapes and first-vertex/index counts. When the
  owner is a CPU-only sorting array, the binding now carries a borrowed
  CPU pointer (and vertex FVF/stride) to the selected renderer.
- The DX12 backend copies CPU sorting vertices and 16-bit indices
  *synchronously at binding time* into fence-protected upload-page slices,
  then publishes native IA views. The original CPU arrays can be reused
  or destroyed without affecting the recorded GPU data. FVF validation
  uses R03's native input-layout decoder.
- The typed `Renderer::Bind_Legacy_{Vertex,Index}_Buffer` overloads now live
  beside the original W3D buffer classes, using opaque Renderer handles
  for standard buffers and CPU sorting snapshots where applicable. Neither
  the native DX12 backend nor its geometry manager imports D3D8 interfaces.
- Sorting pool flushing, segment lines, streaks, point groups, dynamic
  meshes, terrain particles, terrain tracks and water tracks are wired to
  these safe dynamic or legacy buffer binding paths.
- Water tracks write into their persistent dynamic vertex buffer after
  the original initial bind. R02 rebinds after map/unmap so DX12 sees the
  updated immutable version, while the existing DX8 behavior is preserved.
  Its conservative full-buffer snapshot is correct; fine-grained producer
  optimization can be part of subsequent rendering/residency work.
- `Set_Index_Buffer_Index_Offset` now preserves the **base-vertex-index**
  semantics of the legacy name; it is not an index-buffer byte offset.
  The eventual native draw path uses this value in DrawIndexedInstanced.

## Verification

- [DX12 W3D Shader Validation — successful](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38962111432):
  source contracts, HLSL, native Win32/x64 WARP tests. GPU readback checks
  DISCARD/NO_OVERWRITE generation isolation, the first/index suballocation
  ranges, recycled pages, cross-frame rebinding, CPU sorting source rewrites,
  page growth and shutdown/recreation.
- [W3D Renderer Validation — 7/7 successful](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38962111697):
  DX8 dependency audit and all Generals/Zero Hour normal, preview, and
  DX12-exclusive bootstrap builds/tests.

**Next boundary:** M01/B07 still must implement actual `Draw_Triangles`,
`Draw_Triangle_Strip`, mesh submission, material/PSO state, sorting-state
capture/restore, and frame-output integration into R07 targets.
R02 completion does not mean DX12-only gameplay is playable.
