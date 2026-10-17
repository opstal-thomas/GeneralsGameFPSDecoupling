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
