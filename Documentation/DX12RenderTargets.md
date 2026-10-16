# DX12-R07 — Native offscreen color/depth targets, resolve and switching

**Scope:** Native D3D12 render-to-texture (RTT), depth/stencil attachments,
offscreen output binding, texture sampling transitions and multisampled
color resolving in the selected W3D DX12 backend. **Not** a claim of
playable DX12 Generals / Zero Hour scene parity; draw producers remain
`M01/B07` and GPU readback / blit APIs remain `R08`.

## Architecture and ownership

`DX12W3DRenderTargets.{h,cpp}` is the backend-private offscreen target
manager. It binds the real command list obtained from
`DX12PresentationDevice`; it does not create a separate D3D8 device,
render window or queue. Neutral `Renderer` methods delegate to the
manager:

- `Create_Offscreen_Texture`, `Create_Depth_Texture_2D` and
  `Acquire_Texture_Surface_Level`;
- `Acquire_Current_Color_Surface`,
  `Acquire_Current_Depth_Surface`, `Acquire_Back_Buffer_Surface`;
- `Set_Render_Target_Surfaces` for output changes and
  `Clear` for current offscreen color/depth/stencil;
- acquired GPU `RenderSurfaceHandle` reference, release and describe
  operations. CPU `Lock_Surface`, `Copy_Surface`, and readback remain
  explicitly unsupported for GPU surfaces pending `R08`.

R07 attachment texture handles share `DX12W3DTextures` (R04) allocation
and R05 stable texture identity. An acquired surface holds a reference to
its texture and caches a native RTV/DSV descriptor heap **with an owned
reference to the resource generation**. GPU target generations survive
D3D12 device reinitialization through their texture handle; the next
attachment acquisition/bind recreates the view if necessary. Retained **MSAA**
surfaces additionally recreate their multisampled native color allocation
and RTV descriptor on a new device, rechecking MSAA quality support before
recording commands. The separate one-sample resolve texture is rehydrated
through R04 as usual. Primary HWND surfaces are scoped to one
`RecordingSerial` to reject stale handles after resize or frame change.

Both descriptor heaps and native D3D12 resources used by a recorded
command list are retained on the **same GPU fence** through
`RetainFrameObject` and `RetainFrameResource`. W3D handles can be
released before queue execution without invalidating already-recorded
output views. R07 does not block on `WaitForIdle` for each switch.

## Formats and output modes

| Output | Native resource/view | Notes |
| --- | --- | --- |
| Color RTT | `B8G8R8A8_UNORM` DEFAULT texture, RTV | BGRA8/XRGB8, one mip, one sample; shader-readable after the offscreen pass |
| D32 | `R32_TYPELESS` / D32_FLOAT DSV / R32_FLOAT SRV | Native depth and later depth sampling |
| D24S8 | `R24G8_TYPELESS` / D24_UNORM_S8_UINT DSV / R24_UNORM_X8_TYPELESS SRV | Native stencil clear and depth sampling |
| D16 | `R16_TYPELESS` / D16_UNORM DSV / R16_UNORM SRV | Lower-bandwidth depth |
| 2x/4x/8x MSAA color | `B8G8R8A8_UNORM` multisampled RTV | Capability-checked; resolves to a separate one-sample R04/R05 texture |
| Depth-only shadow pass | No RTV; offscreen DSV, `OMSetRenderTargets(0,...)` | No fake primary color attachment or color clear |

The MSAA path checks `D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS` and
fails on unsupported modes rather than silently changing sample count.
Single-sample and multisample output attachments cannot be mixed. On
leaving an MSAA target, the manager transitions its color to
`RESOLVE_SOURCE`, the shader-readable resolve texture to
`RESOLVE_DEST`, calls `ResolveSubresource`, and transitions the
single-sample result for subsequent SRV binding. Render-target creation
rejects incompatible formats, dimensions and mip levels.

## Command-list/state contract

Every `BeginFrame` enters the existing B05 presentation frame and
registers current primary color/depth resources. Target switching is
validated **before** any resource transition: unsupported/stale handles,
color-versus-depth confusion, mismatched width/height/sample count and
invalid primary-frame surfaces are rejected. D3D12 transitions are
recorded through B05's tracked resource states, not guessed
`COMMON` barriers.

Leaving a color or depth target transitions the output to combined pixel
and nonpixel `SHADER_RESOURCE` state, for shroud/shadow/postprocess
sampling. Binding it for output later transitions back to
`RENDER_TARGET` / `DEPTH_WRITE`. The R05 texture stages reject
simultaneous SRV access to an active writable RTV **or DSV**. Binding a
target while its texture remains selected in a sampling stage is also
rejected before any new output transition.

Depth-only passes expose no current color surface, bind zero RTV
descriptors, and reject color clears while still allowing depth/stencil
clears. Conversely, primary color-only binding clears its RTV without
touching an unbound primary DSV. All in-frame `Renderer::Clear` calls
now honor R07's actual output views; the pre-`Begin_Scene` path still
queues the clear through the existing native primary window.
`Set_Render_Target_Surfaces(nullptr, nullptr)` restores the
game-owned primary RTV/DSV and the saved W3D viewport/scissor, including
after a depth-only shadow-map pass. Frame completion resolves/finishes
outstanding offscreen work and restores primary OM bindings before the
existing backbuffer `PRESENT` transition.

## Verification

`W3DDX12RenderTargetSmoke` uses **real WARP GPU resources and pixel
readback** on both Win32 and x64. It covers single-sample RTT clears,
D24S8 depth/stencil, invalid dimension/sample/mip rejection, MSAA resolve
and exact GPU colors, depth-only shadow-map bindings, premature W3D handle
release, per-fence descriptor/resource retirement, primary HWND resize,
device-reset texture/view recovery, and **retained MSAA target recreation on
the replacement device with GPU resolve/readback**.

`W3DDX12RendererFrameSmoke` also calls the **public neutral `Renderer`**
on the actual borrowed game-style HWND: offscreen color/depth acquisition,
switching, clears, depth-only output, DSV-as-SRV rejection and primary
viewport restoration. `scripts/tests/test_dx12_render_targets.py`
guards these contracts; its checks and all WARP programs run in
[DX12 W3D Shader Validation](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/workflows/dx12-shader-validation.yml).
**Verified 2026-10-16:** [The final DX12 shader/source-contract and
Win32/x64 WARP run](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38876104531)
passed all **10 standalone tests on each architecture**, including both
native RTT and real Renderer-HWND path. [All seven Generals and Zero Hour
Windows build, CTest and DX8 dependency-audit jobs](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38876104586)
passed on the same code revision.

**Additional R07 reset-hardening verified:** [Win32/x64 WARP and contract CI](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38880266572)
passed all ten presentation tests in each architecture, including a
**retained 4x MSAA RTV across device shutdown/recreation and real GPU-resolved
pixel readback**. The matching [seven-job game-build and dependency-audit
matrix](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38880266600)
also completed successfully. This fix retains the original R07 scope; it
does not implement R08 surface readback APIs or M01/B07 gameplay rendering.

## Remaining work

- **M01/B07:** Route authoritative world, terrain, water, shadows,
  shroud and HUD draw commands into R07 attachments. Passing offscreen
  WARP tests does not imply any of those producers currently render.
- **R08:** Native texture/surface GPU copies, CPU-mappable readbacks,
  screenshots, blits and compositor parity.
- **R06:** Pixel-accurate samplers, address modes, mip, alpha and sRGB
  behavior.
- **B06/R09:** Robust device loss handling, high-volume target pooling,
  allocation budget and residency policy.
- **V03/V07:** Complete DX12 gameplay parity and executable-level
  removal of DX8 dependencies.

Legacy DX8 mode remains the playable default and is not modified by
this milestone.
