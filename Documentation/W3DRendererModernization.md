# W3D renderer modernization

## Objective

Modernize Command & Conquer: Generals and Zero Hour's W3D rendering engine
without changing gameplay, simulation timing, W3D assets, maps or the art style.
Preserve the legacy Direct3D 8 backend as the reference image while creating a
path toward an optional Direct3D 12 backend and new rendering features.

**The game still runs with Direct3D 8.** DX12 now has an isolated WARP
geometry probe and a reusable, opt-in WW3D2 device/presentation module
that creates and presents to an actual Windows swapchain. Neither is a
DX12 gameplay backend or changes the game's image quality.

### DX8 call-site consolidation (migration phase)

DX12 game integration is paused while remaining WW3D2/GameEngineDevice
direct DX8 device dependencies are isolated. The next pass routes W3D-level
shader, material, texture and transform selections plus state flushes through
`Renderer.h`, with exact forwarding semantics in `DX8Backend.cpp`.
This does not add any DX12 runtime behavior. Backend-neutral callers must
not name D3D transform ordinals or call `DX8Wrapper` to select shaders,
materials, or textures. Legacy raw D3D8 texture-stage states, device
pointers, buffer types and render targets require separate migrations.
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

### Indexed W3D mesh and specialized material milestone

The DX12 backend now includes an opt-in indexed mesh pass that consumes
RenderTextureCombinerKey and RenderBlendState from Renderer.h.
It stages 16-bit indexed vertex/index buffers into GPU-default heaps,
uploads RGBA8 textures, binds two SRVs and static samplers, and records
DrawIndexedInstanced calls into the presentation device command list.
Each supported two-stage texture-combiner key gets a compiled pixel shader
and cached PSO. WARP tests read actual swapchain pixels to verify
untextured and textured materials, alpha-test rejection, indexed submesh
ranges, and reuse of resources/PSOs after ResizeBuffers.

This is a deliberately narrow renderer subset: no general W3D mesh import
or legacy vertex-layout conversion yet; no depth, fog, lighting, blending,
or bump operators. Unsupported states return explicit errors. Shader source
compilation and long-lived upload staging are developer-prototype choices;
production resources and precompiled shaders still require work.

### Native DX12 device and swapchain milestone

`WW3D2/Backend/DX12/DX12PresentationDevice` is a reusable device and
presentation component (not a second renderer contract). It initializes
a hardware or WARP adapter, direct command queue, flip-discard HWND
swapchain, two back-buffer RTVs, per-buffer allocators, command list and
fences. Its frame lifecycle transitions PRESENT -> RENDER_TARGET ->
PRESENT and offers native command-list access for future W3D draws.
`Resize` waits for GPU completion, releases and recreates swapchain buffers;
zero-sized minimized-window resize is safely ignored.

A standalone Win32 test in `Backend/DX12/Presentation` actually presents
multiple colored frames and verifies the swapchain's GPU pixels before
Present. It tests buffer reuse and resize on the Windows CI WARP adapter,
both in Win32 and x64 builds. CI uploads the standalone smoke binaries for
additional verification on physical Windows hardware.
The main WW3D2 CMake project optionally exposes an
`w3d_dx12_presentation` static library when
`W3D_BUILD_DX12_PRESENTATION=ON`. That target is excluded from default builds
and does not link into the shipping game. No DX12 `Renderer::` method
implementations, runtime backend selection, texture streaming or meshes
are included yet. Those depend on removing the remaining DX8-only draw
paths and should be implemented independently of the game's simulation.

### DX12 WARP device, PSO and first texture-modulated triangle

The isolated `WW3D2/Backend/DX12/Probe` target creates an actual D3D12 WARP
software device, direct command queue and command list, root signature,
graphics pipeline state, RGBA8 texture and render target, shader descriptor
heaps, and an upload/readback path. It compiles the existing W3D unlit
VS/PS shader and draws a texture-modulated triangle offscreen. The test
reads actual GPU pixels and checks that the two-stage combiner produces
the expected color against a cleared background.

The standalone geometry probe remains separate from WW3D2 gameplay and
tests W3D-style pixel output. The native swapchain lifecycle is implemented
independently above. Remaining integration requires persistent texture/
descriptor management, rendering real W3D mesh buffers and an explicit
DX8/DX12 runtime selection with safe fallback. Visual parity and gameplay
still need dedicated hardware validation.

### DX12 vertex/pixel shader smoke compilation

A minimal W3D unlit vertex/pixel shader is available in
`WW3D2/Backend/DX12/W3DLegacyUnlit.hlsl`. It takes a world-view-projection
matrix, two texture/sampler pairs, interpolated vertex diffuse color and
a compile-time-specialized combiner key; it invokes the reusable texture combiner and
performs the legacy alpha-test comparison. A dedicated Windows Actions
workflow compiles its VS/PS entrypoints for Shader Model 5.1 as a first
DX12 shader compatibility check.

This shader **does not run in the game yet** and is not claimed as pixel-
identical to the DX8 renderer. Lighting, specular, fog, bump maps, texture
coordinate generation, and multisample/sRGB conventions still need
integration. The standalone geometry probe and swapchain presentation path
are available, but their draw pipelines are not yet connected to W3D scenes.
No separately shipped shader files are required at this stage; production
shader bytecode can be embedded into the executable later.

### DX12 shader reference and permutation identity

The backend-neutral texture operation and argument IDs are now encoded into
a deterministic two-stage RenderTextureCombinerKey. Each color/alpha
channel fits into one byte; the four channels share a 32-bit key. This
avoids hashing W3D object addresses, C++ padding or native D3D values and
allows a DX12 pipeline cache to select a matching shader permutation.

An initial reusable HLSL implementation lives at
`WW3D2/Backend/DX12/W3DLegacyTextureCombiner.hlsli`. It implements the
common color and alpha arithmetic of the W3D fixed-function pipeline and
uses the exact same numeric operations/input sources as Renderer.h.

**This is not a running DX12 backend.** No W3D draw code yet compiles or
binds the HLSL file. Bump-environment operations and special vendor stage
handling require dedicated DX12 permutations and must not silently use the
generic arithmetic path. The original DX8 backend remains active.

### Thirteenth milestone: fixed-function texture combiner stage boundary

W3D material pixel combiners describe color and alpha operations separately
for each stage. Renderer now defines their operators (modulate, add, blend,
subtract, bump environment map, etc.) and four input sources using typed
RenderTextureStageChannel values. Direct3D texture-stage numeric constants
are translated only in the DX8 backend.

The two ShaderClass variants retain their hardware capability fallback
logic and dirty-mask ordering. Generals submits stage 0/1 through the
typed interface, and Zero Hour follows the same path while retaining its
special Voodoo3 stage 2 compatibility behavior. Stage 2 can bypass the
legacy two-stage DX8 cache using the uncached parameter.

These operator descriptions can become the inputs to DX12 pixel-shader
variants and their PSO keys. No new HLSL shader or graphics API is active
yet; fixed-function DX8 remains the only running backend.

### Twelfth milestone: native-independent vertex-material values

VertexMaterialClass now submits full ambient, diffuse, specular and emissive
RGBA values plus the specular power using RenderVertexMaterialDescription.
The DX8 backend builds a temporary native D3DMATERIAL8 immediately before
the original device SetMaterial call. Both Generals and Zero Hour use the
same interface, including their default unlit material.

The existing W3D material asset class continues storing D3DMATERIAL8
internally for now, so unrelated serialization, CRC, and legacy import code
are unchanged. Renderer's public interface and material upload path no
longer require that native memory layout. This is required when fixed-
function lighting becomes a generated DX12 vertex shader.

### Eleventh milestone: vertex lighting, material color sources and UV stages

Both Generals and Zero Hour now route vertex lighting selection, ambient/
diffuse/emissive material-color source selection, and default UV passthrough
through Renderer. These semantics are independent of D3DMATERIALCOLORSOURCE
and legacy texture-stage constants. The DX8 backend issues the same
render-state calls in the same order as the original implementation.

The W3D vertex-material values and custom texture mapper paths remain on
their original representation. Native D3DMATERIAL8 upload and mapper
transform logic have not been ported; the next modern backend must still
reproduce those vertex input semantics before full visual parity.

### Tenth milestone: depth, blending, raster and alpha state

The two game-specific ShaderClass implementations (Generals and Zero Hour)
now forward their common fixed-function pixel blending and alpha-test
state through a backend-neutral RenderBlendState. The native backend maps
blend factors and compare functions to D3D8. This retains the original
blend/alpha state update order and existing shader-dirty bit behavior,
including the color-write-disable emulation and legacy alpha threshold.

Depth compare/write, culling orientation (including the global inverted
backface mode) and final alpha-test enable also use neutral Renderer
calls. Unlike texture combiners, these states can be represented directly
by modern raster/depth/blend pipeline descriptions.

Special cases remain in the legacy shader implementation: device-dependent
texture combiners, multiple stages, fog, specular and N-Patches. Zero Hour
also has vendor-specific Voodoo3 paths. Those must remain intact until a
modern material/shader implementation can reproduce them visually.

### Ninth milestone: backend-neutral sampler state

W3D's TextureFilterClass now resolves its min/mag/mip filters and U/V address
mode into RenderSamplerDescription before applying them. The DX8 backend
translates these modes to the original D3DTSS_MINFILTER, MAGFILTER,
MIPFILTER, ADDRESSU and ADDRESSV commands in the same order using the
original DX8Wrapper state cache. The anisotropy stage setting is also
routed through Renderer.

The existing runtime graphics settings, texture-filter selection arrays
and DX8 capability fallback decisions remain unchanged; W3D's filter
initialization is still tied to DX8 capability bits. This is a sampler-state
boundary, **not** a complete programmable material or shader port. It
will allow a modern backend to implement the corresponding sampler states
without exposing D3D8 texture-stage constants through Renderer.

### Eighth milestone: image-surface creation and COM ownership

W3D's SurfaceClass now delegates image-surface allocation, file-backed
surface loading, surface description queries, and reference acquisition /
release to the Renderer interface. Its backend-neutral RenderSurfaceHandle
cannot be accidentally passed to texture or geometry operations.

The DX8 backend forwards image-surface creation to the original W3D/DX8
factory, preserving file-format handling and missing-file fallbacks. Native
LockRect, pixel processing, blits and surface attachment pointers are still
contained in SurfaceClass, so this migration changes no pixel algorithms
or game content. A future pass can move mapping and blitting behind the
same backend API before introducing a DX12 implementation.

### Seventh milestone: procedural cube/volume textures and reset tracking

Procedural cube and volume textures now use separate backend-neutral
descriptions and typed creation handles. The DX8 backend preserves the
existing mipmap, pool and format conversion paths. Volume texture dimensions
include depth; cube textures use one edge dimension.

A device-reset problem in the legacy code has also been corrected: default
pool cube and volume textures previously registered an ordinary 2D texture
tracker, which recreated a 2D resource after reset. The tracker now records
the texture dimension and volume depth, recreating the correct kind.

The specialized texture constructors now use the TextureClass base
constructor that does **not** allocate an unrelated 2D texture or register
a second default-pool tracker. This also avoids leaking that unused
allocation when the derived cube/volume resource replaces the pointer.

Existing W3D assets and file-loaded cube/volume texture behavior are
unchanged. Texture binding and reference lifetimes continue through the
previously added Renderer methods.

### Sixth milestone: texture stage binding and base-resource references

TextureBaseClass and its standard 2D/depth texture bindings now select
resources using a generic `RenderTextureHandle` through
`Renderer::Bind_Texture`. This binding handle is distinct from the
`RenderTexture2DHandle` used for allocation, because the stage accepts
cube, volume, ordinary 2D and depth textures. The DX8 backend forwards
these stage bindings through `DX8Wrapper::Set_DX8_Texture`, retaining its
existing texture cache, AddRef/Release logic, stage range handling and
render-state accounting. The existing texturing-disabled branch still
binds null, and texture filtering is applied in the same order.

Central TextureBaseClass destruction, invalidation, and texture-reference
replacement now call backend `Release_Texture` and `Retain_Texture`.
Those refcount methods are deliberately not tied to the render thread
because texture loading can also happen asynchronously. Other direct COM
references in texture loaders, D3D surface operations, shader managers,
and derived texture classes remain intact for now.

This is a deliberate compatibility stage; the texture owners still
contain native DX8 pointers, and some specialized resource paths continue
to reach DX8Wrapper. No texture assets, resource formats, or frame/render
ordering are changed.

### Fifth milestone: procedural 2D textures and render-target allocation

W3D procedural 2D textures, color render targets and depth textures now
request their graphics allocations through two backend-neutral creation
methods. The existing DX8 texture trackers use the same path when recovering
resources after a device reset.

The DX8 backend preserves the old DX8Wrapper texture initialization logic,
including pixel-format negotiation, allocation retries and default/managed/
system-memory behavior. This milestone does not alter texture layouts,
mipmapping rules or W3D texture assets.

Texture owners still retain their native D3D8 pointer representation after
creation; moving texture identity, mapping, binding and destruction behind
the renderer interface is the following step. File/surface-based textures,
cube maps, volume textures and swap-chain backbuffers remain unchanged.

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

### DX8 caller migration: current boundary

Recent refactoring moved the remaining texture-filter/font-atlas capability
queries, texture-loader dimensions/aspect-ratio and render-thread checks behind
`Renderer`. The native DX8 backend remains responsible for exposing those
values; no texture selection or texture-upload semantics changed.

`SurfaceClass` font-atlas copies, region copies, format-converting blits,
stretch copies, and CPU surface LockRect/UnlockRect now go through neutral
`Renderer` commands with typed surface handles and pixel-space rectangles.
The DX8 backend still executes CopyRects/D3DXLoadSurfaceFromSurface and
D3D8 surface locks, retaining source/destination formats and filter modes.

Ring, line, point-group and streak primitives now submit triangle draws,
dynamic VB/IB binding and packed vertex colors through `Renderer`. The
transitional DynamicVBAccessClass/DynamicIBAccessClass objects and buffer
cycling/sorted transparency remain DX8-oriented and are **not** a native
DX12 mesh submission contract yet.

Remaining dependencies include native D3D8 surface ownership/Attach/Detach,
texture loader COM level/face locks and uploads, fixed-function mesh
buffer formats, sorted dynamic geometry, plus terrain/effect/UI modules
that still directly hold D3D8 COM objects or call DX8Wrapper. These
must be addressed before the DX8 backend can be fully replaced.

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
