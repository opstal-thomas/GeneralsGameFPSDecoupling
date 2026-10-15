# DX12-B03 — Primary game-window presentation

The exclusive DX12 backend now binds its native swap chain to the **application's actual primary HWND** passed through `WW3D::Init` / `Renderer::Init`. The adapter does not create a second window, subclass the game window, or construct a D3D8 device.

## Ownership model

`DX12PrimaryWindow` in `WW3D2/Backend/DX12` holds the existing `DX12PresentationDevice` (D3D12 device, swap chain, depth target, command lists and GPU fences). The **application** retains ownership of the HWND, WndProc, message pump, native window styles, input and focus.

- `Attach(ApplicationHWnd, useWarp)` verifies that the HWND is a top-level window belonging to this process, then binds the DX12 swap chain directly to it.
- `Synchronize()` reads the actual client area after game message dispatch and resizes the swap chain and depth target. A minimized or zero-client-area window suspends presentation without treating it as a resize failure.
- Windowed and borderless (`WS_POPUP`) modes both use the same windowed DXGI flip-discard swap chain; the game changes its own styles, and the adapter follows the new client dimensions.
- `PresentDiagnostic()` records and presents one clear frame using the game HWND and the existing DX12 device. `Shutdown()` releases GPU resources but never destroys the game window.
- A destroyed HWND is a failure, not an opportunity to create a replacement. GPU resources are safely released.

**Deliberate limitation:** `Renderer::Init` exercises primary-HWND DX12 initialization and diagnostic presentation, then intentionally returns `false` and tears down the device. This fail-closed behavior remains until **DX12-B04** implements a persistent game rendering lifecycle. Running the B03 exclusive-backend executable as a playable game is not supported.

The normal DX8 renderer and optional secondary DX12 mesh preview are unchanged. DX12-B03 does not implement exclusive fullscreen, full game render/device mode policy, DX12 vertex/texture resources, or real `Begin_Scene` / `End_Scene`. DX12-B04 and B06 will address those.

For a software adapter diagnostic, the exclusive build accepts `W3D_DX12_GAME_WARP=1`. The normal path uses a physical DX12 adapter.

## Tests

The standalone `W3DDX12PrimaryWindowSmoke` test runs on **Win32 and x64** in [DX12 WARP validation](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/workflows/dx12-shader-validation.yml). It verifies a real device and repeated presents on the supplied application HWND, exact client-resolution updates, minimize and restore, decorated-to-borderless transitions, safe shutdown without destroying the HWND, reattachment, and device release when the owner destroys the window.

Run locally with Visual Studio 2022:

~~~powershell
cmake -S Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/Presentation `
  -B build/dx12-primary-window -G "Visual Studio 17 2022" -A Win32
cmake --build build/dx12-primary-window --config Release
ctest --test-dir build/dx12-primary-window -C Release --output-on-failure
~~~

The Windows game-build matrix independently compiles Generals and Zero Hour in DX8, DX12 preview and DX12 exclusive-backend configurations. Source tests assert real-HWND ownership and prohibit an independent preview HWND or DX8 fallback.

## Next: B04

Make the primary HWND device lifetime persistent through `Renderer::Init`/`Shutdown`, connect device selection and client resizing to the game's graphics settings, and implement the first valid native DX12 `Begin_Scene` / `End_Scene` frame and state APIs. DX12-B10's first true game frame follows B04; B03 is **window presentation only**, not a full DX12 game runtime.
