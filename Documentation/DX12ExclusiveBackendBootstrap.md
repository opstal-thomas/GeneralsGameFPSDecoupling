# DX12-B02 — Exclusive renderer backend selection

**Status:** Build architecture only. The DX12-selected game is intentionally
**not playable**. [B03](DX12PrimaryGameWindow.md) now owns and tests native
presentation on the main game HWND, but **B04** must still implement the
persistent frame/render-state path before gameplay.

## Selection modes

`W3D_RENDER_BACKEND` is a CMake cache string with exactly two values:

| Mode | Renderer translation unit | Behavior |
| --- | --- | --- |
| `DX8` (default) | `Backend/DX8Backend.cpp` | Original Generals / Zero Hour renderer; unchanged and usable |
| `DX8` + `W3D_ENABLE_DX12_GAMEPLAY_PREVIEW=ON` | `Backend/DX8Backend.cpp` | DX8 game plus optional secondary experimental DX12 preview |
| `DX12` | `Backend/DX12/DX12Backend.cpp` | Exclusive DX12 Renderer symbol provider; **intentionally rejects Renderer::Init** until B03/B04 |

Selecting DX12 while enabling the DX8-dependent gameplay preview is a
configuration error. DX12 selection requires modern MSVC on Windows and links
the existing DX12 presentation library in preparation for B03.

This is **not** yet a DX12-only application. Non-Renderer engine subsystems
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

**Expected bootstrap behavior (updated by B03):** `Renderer::Init` now
attaches to the actual game HWND, attempts one diagnostic D3D12 present,
then returns `false`, releases the swap chain, and `WW3D::Init` returns
`WW3D_ERROR_INITIALIZATION_FAILED`. It does not create a DX8 rendering
device or silently select the legacy backend. The full game is not ready
to run in this configuration.

## Build and CI acceptance gates

- Both backend files expose precisely the same **168** static `Renderer`
  method definitions, so each can link as the selected exclusive provider.
- CMake selects exactly one source and defines exactly one
  `W3D_RENDER_BACKEND_DX8` / `W3D_RENDER_BACKEND_DX12` compile symbol.
- The Windows reusable CI workflow inspects `compile_commands.json`, failing
  if the selected translation unit is absent or its opposite is present.
- The normal DX8 builds, secondary DX12-preview builds, and two new
  `win32-dx12-bootstrap` builds are independently validated.
- The bootstrap still **does not implement rendering**. Its temporary
  diagnostic stubs are deliberate and must be replaced, not treated as
  acceptable fallbacks, under B03/B04/B07.

## Next dependency chain

**B03 (completed for diagnostic presentation):** See
[primary game-window DX12 ownership](DX12PrimaryGameWindow.md) for the
HWND-bound swap chain, resize, minimize/restore and borderless tests.

**B04:** Implement native `Renderer::Init`, frame start/end, clear,
transforms, viewport and minimal valid presentation; replace the bootstrap
diagnostic implementations. Do not convert unsupported methods into silent
success.

**B10:** Only after B03/B04, demonstrate a real game-window DX12-only
frame without creating a Direct3D8 device. B01 inventory still needs to
be driven down and ultimately validated at V07.
