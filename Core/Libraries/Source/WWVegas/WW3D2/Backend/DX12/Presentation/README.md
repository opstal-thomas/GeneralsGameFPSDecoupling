# DX12 swapchain / presentation milestone

The reusable `DX12PresentationDevice` lives one directory above this test.
It creates a D3D12 device (WARP or a supported hardware adapter), direct queue,
double-buffered flip-discard DXGI swapchain bound to a real HWND, RTV heap,
per-buffer allocators, graphics command list, and GPU fences.

`BeginFrame` transitions the current buffer into render-target state and
optionally clears color and depth; native DX12 W3D draws can be appended to
`ActiveCommandList()`. `EndFrame` transitions the buffer back to PRESENT,
executes the command list, optionally calls `IDXGISwapChain::Present` and
records an individual back-buffer fence. `Resize` waits for GPU idle before
releasing buffers and recreating RTVs. Zero-sized resize notifications are
ignored until the window has a usable client area.

Windows / Visual Studio 2022 standalone test:

~~~powershell
cmake -S Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/Presentation -B build/w3d-dx12-presentation -G "Visual Studio 17 2022" -A x64
cmake --build build/w3d-dx12-presentation --config Release
ctest --test-dir build/w3d-dx12-presentation -C Release --output-on-failure
~~~

The test creates a native Win32 window and uses WARP to clear and present six
frames, read back a center pixel from each *swapchain buffer before presenting*,
recycle command allocators/fences and resize from 96x96 to 128x72.
CI tests both Win32 (the game's target architecture) and x64 on WARP.
Use `W3DDX12PresentationSmoke --hardware` locally to test a physical GPU.
The workflow publishes standalone Win32 and x64 smoke executables as artifacts.

The standalone test and the native `w3d_dx12_presentation` library share the
same `.cpp` device implementation. The exclusive DX12 `Renderer` now uses
this library behind an explicit diagnostic gate (B04); the real game still
has unported geometry and material paths. The playable Generals/Zero Hour
configuration therefore remains DX8.

## W3D indexed mesh validation

The reusable DX12IndexedMeshPass supports 16-bit indexed W3D-style vertices,
GPU-default heap vertex/index buffers, staged RGBA8 textures, specialized
W3D texture-combiner PSOs, two UV channels and alpha testing.
Materials with unsupported blending/bump mapping/color-write modes fail
explicitly rather than silently rendering with incorrect state.

The WARP indexed-mesh CTest exercises two DrawIndexedInstanced submesh ranges,
a textured single-stage pass, a two-stage pass, an untextured pass,
alpha-test rejection, invalid index rejection and PSO/resource reuse after
swapchain resize. CI executes both Win32 and x64 variants.

The workflow artifact contains the executables plus W3DLegacyUnlit.hlsl
and W3DLegacyTextureCombiner.hlsli. Extract them together, then invoke:

~~~powershell
.\W3DDX12PresentationSmoke-Win32.exe --mesh-hardware W3DLegacyUnlit.hlsl
~~~

This standalone test is not a DX12 gameplay mode. It compiles HLSL from
source only in the dev probe; production would embed precompiled shader
bytecode. Lighting, fog, depth, alpha blending, complete W3D vertex layouts
and full asset binding are separate milestones.

## B10 — DX12-only developer launch

`W3DDX12LaunchCheckpoint.exe` is a separate Win32 GUI executable that
compiles and links the **actual** `DX12Backend.cpp` Renderer implementation
without linking any Direct3D 8 library. It owns its main HWND, draws native
D3D12 clear frames and runs until Escape/close; `--warp --frames 12 --ci`
runs a deterministic startup/render/shutdown test. CI also audits the built
Win32/x64 PE import tables for D3D8, verifying that the executable imports
D3D12 and DXGI but not the original graphics API.

See the [B10 launch guide](../../../../../../../../Documentation/DX12OnlyLaunchCheckpoint.md)
for build instructions, acceptance criteria, and the dedicated GitHub Actions
artifact. This launcher is **not** a playable Generals / Zero Hour binary.

## DX12-B05 — Render-pass graph and offscreen barriers

The native B05 `DX12W3DPassGraph` now runs on this same command list: it
sequences opaque world, terrain, water, effects, transparency, shroud, UI
and postprocess; validates pass attachment/texture/UAV hazards; and emits
tracked `ResourceBarrier` transitions before native submission. The primary
HWND enters the opaque stage when BeginScene succeeds. Future W3D providers
must register offscreen resource states, provide their own descriptors and
draw calls, and advance stages explicitly. A dedicated
`W3DDX12PassGraphSmoke` Win32/x64 WARP test exercises the full sequence,
compute-style UAV barriers, GPU offscreen readback and resize recovery.
See [B05 specification](../../../../../../../../Documentation/DX12RenderPassGraph.md).

## DX12-R01 — Native W3D vertex/index buffers

The selected DX12 `Renderer` now delegates its neutral geometry handles to
`DX12W3DGeometryBuffers`. It uploads static geometry into GPU-local D3D12
DEFAULT buffers and keeps dynamic generations in CPU-visible UPLOAD buffers.
Both issue genuine IA vertex/index bindings, and the presentation queue
retains uploaded GPU generations until their submission fence completes.

`W3DDX12GeometryBufferSmoke` validates map/unmap, partial updates,
16-bit index binding, stale-view unbinding, in-flight resource lifetime
and actual GPU-readback contents on both Win32 and x64 WARP.
`W3DDX12RendererFrameSmoke` exercises the same path through the public
W3D `Renderer` API on the primary HWND.

See [R01 native geometry guide](../../../../../../../../Documentation/DX12NativeGeometryBuffers.md).
The authoritative W3D draw pipeline is still B07/M01; dynamically
suballocated index/vertex rings are scheduled separately under R02.

## DX12-R07 — Offscreen RTV/DSV attachments and MSAA resolve

The native `DX12W3DRenderTargets` manager now binds W3D offscreen
BGRA8 color, typed depth/stencil and depth-only shadow outputs through
the **same** primary-HWND B05 command list. View descriptors and texture
generations stay GPU-fence owned. Switching back restores the game's
original viewport and native primary targets, while 2x/4x/8x MSAA
color targets resolve into one-sample shader-readable textures.
Active output textures cannot simultaneously be sampled through R05
stages, and clears honor the currently bound RTV/DSV (including
color-only and depth-only passes).

`W3DDX12RenderTargetSmoke` validates these operations with WARP
GPU pixel readback, D24S8 stencil, invalid format/multisample
rejection, resize and device-reset recovery on Win32 and x64.
The actual `Renderer` game-HWND smoke also exercises offscreen,
depth-only, feedback rejection and primary restoration.
See [R07 native target contract](../../../../../../../../Documentation/DX12RenderTargets.md).
Authoritative scene/shroud/terrain/water draws are still M01/B07.
