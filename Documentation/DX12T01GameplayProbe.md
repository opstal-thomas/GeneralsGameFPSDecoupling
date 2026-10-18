# DX12-T01: Opt-in real-game terrain comparison

Status: implementation of an explicit game-scene opt-in; interactive gameplay/visual parity UNVERIFIED.

This changes the selected DX12 Generals/Zero Hour executable, not a secondary render demo.
Normal exclusive DX12 game startup remains fail-closed. Developer opt-in is W3D_DX12_T01_GAMEPLAY=1.
W3D_DX12_GAME_WARP=1 can optionally select WARP for deterministic software rendering.
The old B04 diagnostic and standalone B10 developer checkpoint remain separate.

## Enable a real WW3D scene

In a Visual Studio 2022 Windows development shell, configure the existing separate
win32-dx12-bootstrap preset for Zero Hour:

~~~powershell
cmake --preset win32-dx12-bootstrap -DRTS_BUILD_ZEROHOUR=ON -DRTS_BUILD_GENERALS=OFF -DRTS_BUILD_ZEROHOUR_TOOLS=OFF -DRTS_BUILD_CORE_TOOLS=OFF -DRTS_BUILD_OPTION_TESTS=OFF
cmake --build --preset win32-dx12-bootstrap
$env:W3D_DX12_T01_GAMEPLAY="1"
$env:W3D_DX12_GAME_WARP="1"  # optional; remove for physical GPU
# Run the resulting DX12-exclusive Zero Hour executable using the usual game
# installation/assets, not the separately built B10 checkpoint.
Remove-Item Env:W3D_DX12_T01_GAMEPLAY
Remove-Item Env:W3D_DX12_GAME_WARP
~~~

For Generals, use a fresh build tree with RTS_BUILD_ZEROHOUR=OFF and
RTS_BUILD_GENERALS=ON. The normal DX8 and secondary DX12 preview builds remain
separate. Do not copy the standalone B10 executable over the game EXE.

The DX12-only path uses the actual game HWND. It avoids DX8's browser
initialization, DX8-only display mode descriptions, DX8 presentation/MSAA
settings, and DX8 terrain buffers. DX8Wrapper::Init and Set_Render_Device
explicitly reject requests when the selected backend is DX12; no DX8
renderer fallback is provided.

## Correcting the old "DirectX 8 required" startup message

The earlier DX12-exclusive developer artifact mapped **every** W3D renderer
init or display-mode failure onto the original `ERROR_INVALID_D3D` dialog.
That localized message incorrectly told users DirectX 8.1 was required.
The corrected executable instead distinguishes:

- **DX12 game probe not enabled**: `ERROR_DX12_PROBE_NOT_ENABLED`. Normal
  DX12-only game startup is deliberately fail-closed. Set the developer
  flag in the environment inherited by the **actual game process**.
- **Native Direct3D 12 initialization failed**: `ERROR_INVALID_DX12`.
  Device attach, game HWND, or resolution configuration failed after the
  flag was enabled. This does **not** imply DirectX 8 is missing.
- **Original DX8 renderer initialization failed**: `ERROR_INVALID_D3D`.
  Only the normal DX8 executable retains its original localized dialog.

To avoid launching an old executable or a launcher that doesn't inherit
your developer environment, open PowerShell in the game's working directory
and invoke the DX12-exclusive **game binary**, not the old preview or the
B10 standalone test app, from the **same PowerShell process**:

~~~powershell
$env:W3D_DX12_T01_GAMEPLAY = "1"
Write-Host "DX12 terrain developer flag: $env:W3D_DX12_T01_GAMEPLAY"
& ".\\<actual-DX12-exclusive-Zero-Hour-game-executable>.exe"
~~~

Replace the placeholder with the actual executable filename from the
downloaded Zero Hour bootstrap artifact and verify the original game data
is present. You can select WARP by setting `W3D_DX12_GAME_WARP=1`,
although the first hardware test should normally leave it unset.
If the message still specifically says that *DirectX 8 is required*
after using the corrected artifact, verify which EXE was launched:
a previous DX12 build and the normal DX8 executable still contain the old
error mapping.

If the new DX12 device-failure message appears, capture DebugView or
debugger `OutputDebugString` lines beginning with `[DX12 startup]` and
`[DX12 backend]`. The native resolution path now reports whether it
rejected an unsupported mode, disagreed with HWND decoration/borderless
style, failed a Win32 resize, or could not synchronize the DX12 swap chain.
The T01 window mode classification uses the same popup **and no caption**
condition as `DX12PrimaryWindow::IsBorderless`. Report that log with
the branch commit and screenshot: we can then fix the exact failure rather
than treating every error as missing DirectX 8.

This is a diagnostics/startup error-handling fix, not proof that the
experimental DX12-only skirmish now runs. T01 visual acceptance stays open.

## DX12-exclusive compile flag propagation fix

A real Zero Hour launch reported ONLY the `Renderer::Init: attaching actual
game HWND`, `attached`, `T01 original W3D scene probe enabled`, and
`native DX12 backend ready` messages, and no visible game frame. Inspection
found the underlying backend-isolation error: `corei_ww3d2` attaches
`W3D_RENDER_BACKEND_DX12` only through a PRIVATE link on `z_ww3d2`.
That definition was therefore **absent** from the compiled Zero Hour/Generals
`WinMain`, `GameEngine`, `GameClient`, and shared `W3DDisplay` /
`HeightMap` translation units. They were selecting the legacy DX8 paths
even while the WW3D2 renderer itself was native DX12.

The original game HWND had already been created and displayed with the
loading bitmap, so **a new secondary render window is not expected**.
The renderer presents into that same HWND. CMake now explicitly defines
`W3D_RENDER_BACKEND_DX12=1` on the game engine, device engine and both
game WinMain targets, exclusively when `W3D_RENDER_BACKEND=DX12` is chosen.
The DX8 build/preview mode remains unchanged. The Windows workflow now
audits the **actual WinMain/GameEngine/GameClient/W3DDisplay/HeightMap
compiler commands** for this flag, rather than only checking that the
DX8Backend source was excluded.

This makes the existing W3DDisplay DX12 gating, no-DX8-browser restriction,
native full-resolution terrain, disabled legacy shader registry, startup
tracing and window-style synchronization active in the real executable.
`WW3D::Init` also logs its remaining startup phases after Renderer::Init,
before returning to W3DDisplay. This does **not** guarantee the first game
Present or working UI; any further blocker must be investigated from the
new trace on the latest rebuilt artifact.

## T01 splash-screen stall diagnostics (new)

The `Install_Final.bmp` splash is painted by the **same main application
HWND** that DX12 later presents into. It is not a separate launcher window.
A splash that remains visible, or disappears before a working game scene
appears, means the original game is not known to have reached its first
native Present. Do not diagnose this as a failed CreateWindow just from
its appearance.

The exclusive DX12 game now writes a persistent startup-stage trace to:

`%TEMP%\\GeneralsDX12Startup.log`

The trace records only static subsystem stage labels, process IDs, relative
Win32 ticks, and native renderer errors. It does not record personal paths,
game profiles or command-line data. Both the W3D display and the earlier
`GameClient` initialization (image loading, font library, cursor resources,
window manager, shell UI and terrain visual) are instrumented. The old
W3DShaderManager registry that initializes DX8-only terrain transitions,
screen filters and legacy render targets is **excluded** in the selected
T01 DX12 game-scene probe. The original DX8 build is unchanged.

For the next Windows retest, close the game and use **the new artifact**
from the latest successful `win32-dx12-bootstrap` GitHub workflow.
Run the actual Zero Hour executable from the installation directory, with
its normal game data available, from the same terminal:

~~~powershell
Remove-Item "$env:TEMP\\GeneralsDX12Startup.log" -ErrorAction SilentlyContinue
$env:W3D_DX12_T01_GAMEPLAY = "1"
Remove-Item Env:W3D_DX12_GAME_WARP -ErrorAction SilentlyContinue
& ".\\generalszh.exe"      # Use the exact filename packaged by the DX12 artifact.
Get-Content "$env:TEMP\\GeneralsDX12Startup.log" -Tail 100
~~~

Use WARP only as a second attempt with
`$env:W3D_DX12_GAME_WARP = "1"`, because it can be much slower.
If the application window closes or remains on the splash, send the
**last 50–100 lines** of `GeneralsDX12Startup.log`. The final stage
distinguishes incomplete INI/client initialization, an early font/image
failure, DX12 adapter/swap-chain attachment, resolution rejection,
unsupported shader/UI initialization, a scene-rendering failure, or a
successful first native HWND Present. A successful Present without
authoritative heightmap tiles is not T01 acceptance.

This is an instrumented developer startup probe, not a claim that Zero Hour
now launches a playable DX12 skirmish. Only actual gameplay screenshots
and renderer diagnostics can validate T01. Preserve V03, T02 and the
remaining UI/effects milestones independently.

## Evidence of real terrain

Capture the game's OutputDebugString messages (Sysinternals DebugView or
a native debugger) while opening a skirmish. Required messages include:

  [DX12-T01] EXCLUSIVE DX12 gameplay scene probe enabled
  [DX12-T01] Native game HWND Present count=N
  [DX12-T01] Original heightmap frame=N tiles=N triangles=N atlas=native

Only nonzero tiles and triangles together with atlas=native demonstrate the
original HeightMapRenderObjClass submitted a native terrain base pass.
A Present message alone proves frame presentation, not a terrain render.
Any BLOCKED legacy DX8Wrapper device creation message fails backend isolation.

Match game data, map, resolution, camera position, and graphics settings
against a normal DX8 build. Capture corresponding screenshots in Generals
and Zero Hour, including cliffs, slopes, right/bottom map boundaries,
adjacent tiles, atlas materials, dynamic local lighting and panning/rotation.
Check moving-light color restoration and updated heightmap geometry.
Archive screenshots, GPU vendor, commit SHA and diagnostics.

## Limits and failures

This is a developer test route and NOT an indication that a skirmish now works.
The selected W3D renderer still has unported specialist render methods and
non-Renderer DX8 callers. Startup may fail or render incompletely.
The legacy game binary may still import d3d8.dll; DX12-V07 must audit every
remaining runtime import and device-creation path, not just DX8Wrapper.
If startup crashes or no original terrain appears, report the last
OutputDebugString lines and keep T01 visual acceptance OPEN.

CI can build Generals/Zero Hour and run deterministic WARP heightmap tests,
but without the installed game data and an interactive skirmish it cannot
validate in-game terrain parity. T02 textures, T03 water, T06 shroud,
T07 roads, U03 UI and V03 playable single player remain independent.

## Deliberately excluded non-T01 renderers

During this selected DX12-only developer game-scene probe, the game leaves
legacy water initialization and rendering (T03), shroud GPU updates (T06),
and projected/stencil shadows (T09) out of the scene. These are NOT DX12
implementations or silent DX8 fallbacks: they remain separate incomplete
milestones, while genuine terrain and supported original W3D meshes still
use native DX12 submission. This avoids dereferencing the null DX8 device
in WaterRenderObjClass::ReAcquireResources. All normal DX8 game builds
retain the original water/shroud/shadow behavior.

## 2026-10-18: Zero Hour terrain-init isolation and first-frame diagnostics

The latest installed Zero Hour trace on commit `35fcba0` reaches
`GameClient: initialize original terrain visual`, **not** the subsequent
`GameClient: terrain visual initialized` stage. This narrows the next
investigation to `W3DTerrainVisual::init()`, but a log tail alone does not
prove which call (if any) crashes, throws or stalls.

Exclusive DX12 T01 now instruments that original initialization routine
and retains terrain-track and smudge *CPU bookkeeping* while omitting their
unported GPU buffer and framebuffer-copy effect paths. Shadow and water
rendering remain out of the existing T01 probe. All optional GPU-effect
omissions are compile-time DX12-only, with no DirectX 8 fallback. These
effects are **not** declared migrated or rendered by this checkpoint.
The legacy-named DX8Vertex/IndexBuffer classes can already be backed by
native Renderer buffers; their names alone do not prove D3D8 dispatch.

Inspect the following ordered checkpoints in
`%TEMP%\GeneralsDX12Startup.log` for the **same process ID**:

1. `W3DTerrainVisual::init entered`
2. `W3DTerrainVisual: original heightmap render object constructed`
3. `W3DTerrainVisual: terrain-track CPU manager initialized`
4. `W3DTerrainVisual: shadow manager bookkeeping constructed (T09 GPU path omitted)`
5. `W3DTerrainVisual: smudge CPU manager initialized`
6. `W3DTerrainVisual::init completed; returning to GameClient`
7. `GameClient: terrain visual initialized`
8. `W3DDisplay::draw first game-frame entry`
9. `W3DDisplay: first WW3D::Begin_Render returned OK`
10. `Renderer::Begin_Scene: first native game HWND frame requested`
11. `Renderer::Begin_Scene: first native command list and render targets ready`
12. `W3DDisplay: first drawViews completed` (only after the loading-screen pass)
13. `W3DDisplay: first scene/UI frame reached WW3D::End_Render`
14. `Renderer::End_Scene: first actual native game HWND present succeeded`

`Begin_Render returned OK` is **not** proof of successful Begin_Scene or
present. The original `HWND` is the splash window itself and should become
the game window; no second preview window will appear. Native first Present
does **not** prove original terrain rendering. Open an actual skirmish and
capture both nonzero `Original heightmap frame=N tiles=N triangles=N
atlas=native` and `Native game HWND Present count=N` in DebugView.
Check terrain atlas material mapping and scrolling against the DX8 executable.

Use the latest successful Zero Hour `win32-dx12-bootstrap` artifact from
the renderer-validation Actions run at the new commit. Do not mistake the
separate WARP/B10 checkpoint for the original Zero Hour game executable.
Set `W3D_DX12_T01_GAMEPLAY=1` in the process environment and clear the old
startup log before launch. A missing stage identifies the last observed
initialization boundary, **not** automatically its root cause.

The Windows CI audit now checks backend definitions in the actual compile
commands for the original terrain visual, terrain tracks and smudge
translation units in both normal and selected DX12 builds. The static
tests and WARP smoke cannot substitute for installed-game gameplay evidence.
T01 acceptance stays open until real native presentation and authoritative
map atlas comparisons succeed.
