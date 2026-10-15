# DX12-B02 — Exclusive renderer backend selection

**Status:** Exclusive build selection complete. The DX12-selected game is
**not playable**. [B03](DX12PrimaryGameWindow.md) implements game-HWND
presentation; [B04](DX12NativeRendererFrame.md) adds persistent native frame
and state APIs under a developer diagnostic opt-in. Full resource parity
and the launch checkpoint are still pending (B07/B10).

## Selection modes

`W3D_RENDER_BACKEND` is a CMake cache string with exactly two values:

| Mode | Renderer translation unit | Behavior |
| --- | --- | --- |
| `DX8` (default) | `Backend/DX8Backend.cpp` | Original Generals / Zero Hour renderer; unchanged and usable |
| `DX8` + `W3D_ENABLE_DX12_GAMEPLAY_PREVIEW=ON` | `Backend/DX8Backend.cpp` | DX8 game plus optional secondary experimental DX12 preview |
| `DX12` | `Backend/DX12/DX12Backend.cpp` | Exclusive DX12 Renderer; **normal game initialization remains fail-closed** until the native WW3D resource/draw migration; B04 developer diagnostic available |

Selecting DX12 while enabling the DX8-dependent gameplay preview is a
configuration error. DX12 selection requires modern MSVC on Windows and links
the native DX12 primary-window presentation library.

This is **not** yet a DX12-only Generals / Zero Hour game. Non-Renderer engine subsystems
still compile legacy DX8 dependencies (see [DX8 dependency inventory](DX8DependencyInventory.md)).
The DX12 bootstrap never falls back to DX8 **through Renderer**. Until the
remaining dependencies are removed and the real game device is implemented,
do not advertise, install, or execute the DX12 bootstrap artifact as a
playable replacement.

## Configure and build

Use the dedicated, separate `win32-dx12-bootstrap` build directory so
normal DX8 and DX12-preview artifacts cannot be confused. Example for
Zero Hour (in a VS2022 x86 developer command prompt):

~~~powershell
cmake --preset win32-dx12-bootstrap -DRTS_BUILD_ZEROHOUR=ON -DRTS_BUILD_GENERALS=OFF -DRTS_BUILD_CORE_TOOLS=OFF -DRTS_BUILD_ZEROHOUR_TOOLS=OFF -DRTS_BUILD_OPTION_TESTS=OFF
cmake --build --preset win32-dx12-bootstrap
~~~

The default playable DX8 reference is unchanged:

~~~powershell
cmake --preset win32 -DW3D_RENDER_BACKEND=DX8
cmake --build --preset win32
~~~

For the secondary DX12 preview, keep `W3D_RENDER_BACKEND=DX8` and separately
enable `W3D_ENABLE_DX12_GAMEPLAY_PREVIEW=ON`.

**Expected bootstrap behavior (updated by B04):** `Renderer::Init`
attaches the DX12 device to the actual game HWND. Without
`W3D_DX12_B04_DIAGNOSTIC=1`, it intentionally returns `false` and releases
the swap chain, so `WW3D::Init` reports initialization failure. With
the explicit developer-only opt-in, B04 keeps the primary device alive
and provides frame/clear/viewport/transform APIs. **This does not make
normal gameplay playable**: numerous W3D resources and draws are still
unsupported and the game must not be shipped in this mode.

## Build and CI acceptance gates

- Both backend files expose precisely the same **168** static `Renderer`
  method definitions, so each can link as the selected exclusive provider.
- CMake selects exactly one source and defines exactly one
  `W3D_RENDER_BACKEND_DX8` / `W3D_RENDER_BACKEND_DX12` compile symbol.
- The Windows reusable CI workflow inspects `compile_commands.json`, failing
  if the selected translation unit is absent or its opposite is present.
- The normal DX8 builds, secondary DX12-preview builds, and two new
  `win32-dx12-bootstrap` builds are independently validated.
- B04 implements **native frame/state and clear operations**, validated
  against WARP. B10 adds a [standalone DX12-only developer launcher](DX12OnlyLaunchCheckpoint.md)
  without a D3D8 device. Authoritative W3D geometry/assets still require
  R01/R04/M01/B07; unsupported game methods remain explicit and do not
  silently fall back to DX8.

## Next dependency chain

**B03 (completed for diagnostic presentation):** See
[primary game-window DX12 ownership](DX12PrimaryGameWindow.md) for the
HWND-bound swap chain, resize, minimize/restore and borderless tests.

**B04 (complete native diagnostic):** See [native Renderer frame/state
checkpoint](DX12NativeRendererFrame.md) for `Renderer::Init`, selective
pre-frame clearing, conditional present, transforms, viewport and native
device queries. The developer-only gate prevents unsupported gameplay
paths from being treated as complete.

**B10 (standalone DX12-only launch checkpoint):** The dedicated
[developer GUI executable](DX12OnlyLaunchCheckpoint.md) creates a primary
HWND and presents frames using the real DX12 Renderer implementation, without
loading or importing D3D8. This does **not** enable normal game startup:
the B01 inventory still must be driven down and validated by V07, and
gameplay asset/draw paths remain open under R01/R04/M01/B07.
