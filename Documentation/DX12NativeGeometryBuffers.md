# DX12-R01 — Native W3D vertex/index buffers

**Scope:** The exclusive DX12 renderer now implements the backend-neutral
`RenderVertexBufferHandle` and `RenderIndexBufferHandle` APIs with **native
D3D12 allocations** and byte-range map/unmap and IA binding. The DX8 backend
continues to own the default, playable game configuration.

## Allocation and ownership

The implementation is in
`Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12W3DGeometryBuffers.{h,cpp}`.
`DX12Backend.cpp` delegates real `Renderer::Create/Release/Map/Unmap/Bind`
calls to these backend-private resource objects. The handle types in
`Renderer.h` stay opaque; neither game code nor WW3D2 callers receive
D3D8 COM interfaces.

| W3D allocation | GPU storage | Write behavior |
| --- | --- | --- |
| Static vertex/index | **D3D12 DEFAULT heap** | CPU byte shadow; upload through an **UPLOAD staging resource**, `CopyBufferRegion`, and tracked `COPY_DEST → VERTEX_AND_CONSTANT_BUFFER / INDEX_BUFFER` transitions on the active command list |
| Dynamic vertex/index | **D3D12 UPLOAD heap** | CPU byte shadow; each committed write creates a **new** GPU-visible version and uses `GENERIC_READ` without an invalid upload-heap resource transition |
| Point sprites | Dynamic XYZ vertex handle | `Create_Point_Sprite_Vertex_Buffer` creates native geometry; point rasterization and pipeline state are separate M11/B07 work |

Static buffers are allocated on first **native bind**, not during W3D
construction: W3D often creates and fills geometry before it begins a
rendering scene. They become GPU-local when first submitted and are not
lazily uploaded into an unrelated DX8 device.

## W3D mapping and binding

- `Map_Vertex_Buffer` and `Map_Index_Buffer` use **byte offsets** and
  **byte lengths**. Length zero means the remainder of the buffer.
- Map/unmap rejects out-of-range and nested maps, invalid flags, double
  unmaps and binds while mapped. Read-only maps preserve the previous
  generation. Append/partial writes preserve unaffected bytes.
- `RENDER_MAP_DISCARD` and `RENDER_MAP_NO_OVERWRITE` are supported for
  dynamic handles. Writes never overwrite a GPU allocation referenced by
  a previous command list, including multiple writes within one frame.
  This version uses **whole-resource versioning**; R02 will provide more
  efficient suballocated rings, DISCARD/NO_OVERWRITE offsets and batching.
- A static write after earlier draws creates a **fresh GPU-local resource**.
  Earlier command-list uses remain valid until the queue fence completes.
- `Bind_Vertex_Buffer` and `Bind_Index_Buffer` submit actual
  `IASetVertexBuffers` / `IASetIndexBuffer` views. Sliced dynamic handle
  overloads preserve `firstVertex/firstIndex` byte offsets, and the W3D
  indexed base vertex is retained for subsequent `DrawIndexedInstanced`
  support (M01/B07). The existing fixed 16-bit W3D index format uses
  `DXGI_FORMAT_R16_UINT`.
- Existing W3D lock containers in `dx8vertexbuffer.cpp` and
  `dx8indexbuffer.cpp` now assert the *selected* DX12 Renderer, not the
  absent DX8 device, when building exclusive DX12. They retain the original
  DX8 checks on the DX8 build. Sorting/dynamic ring allocation still
  needs R02; R01 does not make the legacy WW3D2 geometry engine DX12-only.

## GPU lifetime and B05 integration

`DX12PresentationDevice::RetainFrameResource` holds a COM reference to each
GPU vertex/index buffer generation **and** its staging upload until the
recorded frame's queue fence reports completion. Deleting a W3D handle
before `End_Scene` no longer releases an allocation still used by the
in-flight command list. Old versions are retired without calling
`WaitForIdle` for every frame; resize/shutdown performs appropriate
synchronization and clearing. A queue-signal failure keeps resources
pinned until teardown rather than destroying allocations still referenced
by already-queued GPU commands.

Static buffers register their actual initial `COPY_DEST` state with the
B05 pass tracker before copying, then transition into vertex/index read
states. Reused resources are re-registered on later frames with their
actual final state. Dynamic upload heaps retain `GENERIC_READ` throughout
their GPU lifetime.

## Verification

The new `W3DDX12GeometryBufferSmoke` uses WARP to verify static DEFAULT and
dynamic UPLOAD memory types, direct IA binding, 16-bit index views, full and
partial maps, illegal-map rejection, DISCARD/NO_OVERWRITE generation
isolation, real static-geometry GPU readback, partial static updates and
fence-protected early handle release. It also checks frame reuse/resizing.

The existing `W3DDX12RendererFrameSmoke` now creates, maps, binds and
releases geometry through **the actual neutral Renderer API** on the
game-style primary HWND. Both tests execute on Win32 and x64 in
[DX12 W3D Shader Validation](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/workflows/dx12-shader-validation.yml).
`scripts/tests/test_dx12_geometry_buffers.py` verifies the source-level
backend contract in both CI workflows.

**Verified 2026-10-16:** [Win32/x64 WARP shader, native Renderer and GPU
geometry-residency workflow](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38826379094)
passed, including the final null-unbind regression. [All seven Generals
and Zero Hour Windows build/dependency-audit
jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38826379107)
also passed.

**Limitations:** W3D's old renderer may still reference D3D8 for FVF
translation, draw batches and material/texture state; these are R03, R04
and B07/M01 tasks. This checkpoint provides **native GPU geometry
resources**, not playable DX12 rendering or a complete indexed drawing
pipeline. R02 owns scalable dynamic buffer rings, and R09 owns resource
streaming/eviction.
