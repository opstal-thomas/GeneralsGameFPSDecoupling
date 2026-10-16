# DX12-R05 — Native W3D texture binding and descriptor lifetime

**Scope:** Stable DX12 2D/cube/volume texture identities, two-stage shader-visible
SRV bindings, resource-update invalidation and fence-protected descriptor
ownership. R05 builds on [R04 native asset ingestion](DX12NativeTextures.md)
and B05 tracked transitions. It does not claim a DX12-only playable match.

## Native W3D binding

`Renderer::Bind_Texture(stage, texture)` and
`Renderer::Bind_Immediate_Texture(stage, texture)` both retain handles in
the exclusive DX12 backend's `DX12W3DTextureStages` cache. A bound stage
owns one reference, so the loader may release its original reference
without invalidating the binding. All eight legacy stage indices are
validated; the current unlit shader's two texture inputs use stages 0/1.
`Renderer::Release_Texture_Resources` and `Renderer::Shutdown` clear the
cache without touching the DX8 device.

`DX12W3DRendererTextureStages().BindForDraw(presentation, firstRoot)`
is the backend-private integration for M01/B07 native draw submission.
The draw producer must first install its matching root signature, exposing
a separate SRV table at `firstRoot` and `firstRoot+1`, then invoke
`BindForDraw` immediately before recording its draw command. This call
sets the shader-visible heap and **both** descriptor tables after every
heap change. The DX12 backend does not pretend pending gameplay draws
exist; their authoritative integration remains an explicit dependency.

The indexed mesh **developer preview** now uses the same
`DX12W3DTextures` and `DX12W3DTextureStages` implementations instead
of maintaining its own texture allocations and descriptor heap.
Preview RGBA8 source rows are converted to W3D's BGRA memory order before
entering the R04 uploader. Existing mesh preview geometry and two-stage
material combiner semantics are preserved.

## Safe descriptors and ownership

- Each R04 texture gets a monotonically allocated 64-bit **stable
  identity**, unrelated to its CPU address. Unmap/Update advances its CPU
  `contentRevision`; GPU resource re-upload and device reinitialization
  advance `gpuGeneration`. A view snapshot contains a strong native
  `ID3D12Resource` reference and the complete 2D/cube/3D SRV shape,
  including all available mip levels.
- A material's descriptor-table key is the two textures' identities,
  content revisions and GPU generations. Updating a texture produces
  a new GPU allocation and immutable table; previous draws continue
  sampling their old snapshot until the GPU fence completes. No cache
  lookup keys solely on a potentially recycled pointer.
- Null stage binding writes an actual **D3D12 null SRV** (zero/black)
  instead of leaving an old descriptor in the pipeline. A stage clear
  releases its CPU handle without rewriting in-flight descriptors.
- Shader-visible CBV/SRV/UAV heaps are organized as immutable pages of
  **512 descriptors (256 two-stage material pairs)**. A new page is
  allocated as needed; old descriptor memory is never overwritten within
  the recording. Identical material pairs in one recording reuse their
  already-written table, avoiding per-draw descriptor heap allocation.
  For now R06 owns additional texture stages and sampler variants.
- The B05 frame serial prevents lookup into descriptors from a previous
  command recording. The stage cache revalidates the B05 resource state
  after readbacks/other pass transitions rather than assuming its earlier
  SRV transition is permanent.
- `DX12PresentationDevice::RetainFrameObject` preserves the COM descriptor
  heap on the **same signaled queue fence** as the GPU texture allocations
  retained by `RetainFrameResource`. Rebinding, unbinding, releasing the
  final W3D CPU reference or resetting the stage cache cannot leave a
  recorded draw with a freed descriptor heap. Completed fences retire
  both object and resource references.

## Tests and compatibility

`W3DDX12DescriptorSmoke` uses a real native D3D12 WARP command list,
pixel shader and GPU backbuffer readback. It samples separate red and blue
stage bindings, rewrites the original texture in the same frame and
confirms the earlier red draw remains intact while the later draw samples
green. A subsequent null binding samples black. The test releases the
last W3D CPU owners **before** submission, checks fence retirement, then
reinitializes the device and rebuilds descriptors against its new
resource generation.

It also creates **260 distinct material-table snapshots** in one frame,
forcing heap-page rollover. GPU readback verifies both the original draw
from page one and the later draw from page two, even after last-owner
release and cache clear. Cube and volume SRV dimensions/mip counts are
validated independently. The pre-existing indexed-mesh preview WARP test
also exercises this shared native binding path.

The DX12 shader CI workflow runs the complete smoke suite on **Win32 and
x64**. `scripts/tests/test_dx12_texture_descriptors.py` checks shared
preview/Renderer integration, stable GPU generations, null descriptors,
fence retention and overflow coverage. All standard DX8 game builds
continue using the unchanged DX8 backend.

## Deliberate exclusions

- **M01/B07:** True W3D world/terrain/water/UI draw submission using
  this root-table interface. The preview and WARP smoke do not constitute
  playable DX12 gameplay.
- **R06:** Exact sampler state, texture addressing, mip selection, legacy
  bump and alpha/color-space parity. R05 binds SRV resources; it does not
  silently emulate all D3D8 fixed-function texture stages.
- **R07/R08:** Render-target/depth attachments, GPU texture-to-surface
  copies, screenshots and readbacks for gameplay surfaces.
- **R09:** Batching, streaming, GPU residency policy and eviction at
  scale. R05 avoids a per-draw heap allocation but retains the legacy
  preview's independent per-frame idle behavior until that workstream.
- **V03/V07:** Real skirmish parity and audited elimination of all D3D8
  process imports/compatibility ABI pointers.
