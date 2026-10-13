# W3D renderer modernization

## Objective

Modernize Command & Conquer: Generals and Zero Hour's W3D rendering engine
without changing gameplay, simulation timing, W3D assets, maps or the art style.
Preserve the legacy Direct3D 8 backend as the reference image while creating a
path toward an optional Direct3D 12 backend and new rendering features.

**This branch does not implement Direct3D 12 or change image quality.** It
strengthens the existing backend boundary so that later work can change the
graphics implementation without changing game logic.

## Existing architecture

The repository already has `WW3D2/Renderer.h` and
`WW3D2/Backend/DX8Backend.cpp`. The build links exactly one implementation
of these static entry points. `WW3D::Init`, `WW3D::Shutdown` and several
frame operations already use this interface.

This is the supported seam. Do not introduce a second competing rendering
abstraction in GameEngineDevice or bypass it with a new DX12 wrapper.

```text
GameEngine / GameEngineDevice (drawables, views, terrain, UI)
                       |
                     WW3D2
           (objects, materials, meshes, scenes)
                       |
               Renderer.h [neutral API]
                       |
           Backend/DX8Backend.cpp [reference]
                       |
                    DX8Wrapper
                       |
                    Direct3D 8
```

### Third milestone: vertex and index buffer allocation ownership

The W3D vertex and 16-bit index buffer classes now request GPU allocations
through `Renderer::Create_Vertex_Buffer` and
`Renderer::Create_Index_Buffer`, and release them through corresponding
backend methods. The DX8 backend owns its D3DUSAGE, D3DPOOL and D3DFMT
translation. The public description carries a byte count, the existing W3D
vertex format code and backend-neutral usage flags.

The resource allocator retains the previous recovery behavior: failed
vertex allocation invalidates old textures and the mesh cache, discards
managed DX8 resources, and retries; index allocation invalidates the
textures/mesh cache and retries without the managed-pool discard. An
unsuccessful second attempt still raises a fatal assertion.

Buffer mapping now also goes through `Renderer::Map_Vertex_Buffer` and
`Renderer::Map_Index_Buffer`, with corresponding unmap operations.
The abstraction expresses discard, no-overwrite, no-system-lock and
read-only modes independently of D3D8 flag values; the DX8 backend
translates them. Full-buffer, ranged append and dynamic ring-buffer
mapping retain their existing offsets, sizes and flags.

The vertex and index buffer resource classes now hold distinct opaque,
strongly typed handles rather than native D3D8 resource pointers.
Creation, release, map/unmap and device binding all use those handles.
DX8 COM pointers are recovered only inside the DX8 backend. The legacy
DX8-named accessors are temporarily preserved for any unmigrated callers.

The existing DX8Wrapper state cache still retains vertex/index buffer
references, dynamic ring offsets, deferred state changes and draw-order
semantics. Its actual SetStreamSource/SetIndices submissions are now
performed by Renderer::Bind_Vertex_Buffer and Bind_Index_Buffer.

This is an intentional intermediate design: the handles are opaque to
callers, but the DX8 implementation encodes native resource addresses.
They are not yet backend-independent lifetime-managed object identities.
Migration of texture/surface resources and the DX8Wrapper state cache
comes next. Texture/surface allocation and
resource-reset ownership are also not migrated yet.

There are no new resource file formats, shader binaries or DLL dependencies.

### Second milestone: mesh submission and resource capabilities

The high-level WW3D scene now routes camera selection, mesh-batch flushing,
mesh-cache invalidation and pending mesh cleanup through `Renderer`. This
preserves the original flush sequence, including shader processing and sorted
transparency, while moving backend-specific mesh handling to `DX8Backend.cpp`.

A neutral `RenderCapabilities` struct provides optional GPU feature flags.
The DX8 implementation maps those from `DX8Caps` without leaking D3D8 types
into callers. Actual static/dynamic vertex and index buffer usage now checks
the backend-reported transform-and-lighting / N-Patches capabilities. Texture
manager shutdown is also routed through the backend without changing when
it runs relative to device shutdown.

This milestone does **not** change resource ownership or allocation semantics:
D3D8 vertex/index buffers, surfaces and textures are still allocated and
locked by legacy code. That is the next migration boundary. Keeping this
intermediate stage lets us validate real W3D mesh submission before changing
resource lifetime management.

### First milestone: device and raster boundary

The DX8-backed `Renderer` interface now owns the following operations:

- Adapter selection (by index/name), device enumeration, windowed/fullscreen
  selection and resolution queries/changes.
- Swap interval selection, preserving the existing presentation policy.
- Device readiness, loss and reset-required states without exposing D3D
  HRESULTs to WW3D frame control or W3DDisplay view updates.
- Point/line/solid fill state, translated to D3D8 only in DX8Backend.cpp.

The legacy `WW3D::Begin_Render` behavior is preserved: a lost device skips
the frame, a reset-required device attempts one reset and skips that frame,
and the next presentation can retry. An absent device retains the legacy
lite/headless behavior. W3DDisplay still skips view preparation without a
ready hardware device.

`WW3D::Get_Render_Device_Desc` intentionally remains a legacy exception:
`RenderDeviceDescClass` embeds `D3DCAPS8` and
`D3DADAPTER_IDENTIFIER8`. A future milestone must replace that type with
a backend-neutral adapter/capability description before DX8 can be removed.

There are **no new allocations, GPU resources, shaders, rendering passes or
asset conversions** in this milestone. DX8 backend methods forward to the
same `DX8Wrapper` calls and preserve their return codes.

## Target boundaries

| Layer | Owns | Must not own |
| --- | --- | --- |
| GameEngine | Deterministic simulation, drawable state, commands | D3D device/API |
| W3D / WW3D2 | Visibility, scenes, meshes, material intent, draw order | Native D3D resource lifetimes |
| Renderer contract | Backend-neutral device, resource, pipeline and command concepts | D3D8/D3D12 types |
| DX8 backend | Translating Renderer operations to DX8Wrapper | Game simulation |
| Future DX12 backend | Device/queues, resource heaps, descriptors, root signatures, PSOs, barriers, presentation | Gameplay logic |

### Known remaining Direct3D 8 dependencies

- `WW3D2/dx8wrapper.h` is still accessed directly by mesh, material, texture,
  terrain, water, shadows and several effects.
- Mesh submission, flush and invalidation in WW3D now go through
  `Renderer`, while lower-level mesh and geometry classes still use DX8 types.
- `W3DShaderManager` owns `IDirect3DTexture8`/`IDirect3DSurface8`
  and uses the legacy fixed-function texture-stage API.
- `RenderDeviceDescClass` exposes D3D8-specific adapter structures.
- DX8 reset still owns legacy resource recreation. A modern renderer will
  need explicit ownership, barriers, lifetime tracking and swap-chain handling.

These must be migrated deliberately: simply forwarding new D3D12 calls through
the old fixed-function interface would not create a modern renderer.

## Proposed incremental roadmap

1. **Adapter / capabilities:** Replace the legacy DX8-caps-based public
   device description with a neutral format and capability query.
2. **Resources:** Introduce backend-independent texture, vertex/index buffer,
   render-target and depth resources; preserve W3D asset decoding.
3. **Materials / shader pipeline:** Describe material blending, culling,
   depth, vertex layouts and texture stages as explicit immutable pipeline
   state. Reproduce legacy fixed-function results with programmable shaders.
4. **Submission:** Redirect geometry and terrain draw calls to the Renderer
   contract. Then migrate water, particles, shadows, overlays and UI.
5. **DX12 backend:** Implement resource heaps, command lists, descriptor
   management, barriers, fences, pipeline state objects and swap-chain
   handling alongside the DX8 backend.
6. **Image quality:** After visual parity, add optional modern shadows,
   anti-aliasing, ambient occlusion, improved water and post-processing
   using the existing source art.

## Acceptance checks for this branch

- Win32 CMake build and GoogleTest pass for **both** Generals and Zero Hour.
- Game starts, menus display, skirmishes render, and the game remains
  playable with the original DX8 backend.
- Alt-tab/minimize/restore and fullscreen/windowed/resolution changes still
  recover correctly.
- UI, map terrain, water, shadows, particles and scripted cameras look
  unchanged. Capture reference screenshots of representative scenes for
  later DX12 parity comparisons.
- Headless mode, replay playback and simulation remain unaffected.

Automated contract tests verify backend-neutral device-state policy only.
They **cannot** validate real GPU behavior; gameplay and graphical parity
still need Windows hardware testing. Do not merge this branch solely because
CI is green.
