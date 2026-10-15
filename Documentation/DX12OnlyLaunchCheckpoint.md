# DX12-B10 — First DX12-only executable launch checkpoint

**Scope:** A self-contained **Windows developer executable** uses the
actual exclusive `WW3D2/Backend/DX12/DX12Backend.cpp` implementation of
`Renderer`. It creates its application's **primary HWND**, initializes a
persistent native D3D12 device and DXGI flip swap chain, records clear frames,
presents, handles resizing and message dispatch, and exits cleanly. This is
the first `DX12` **render-backend launch** independent of the legacy Direct3D 8
device. It is **not** a playable Generals or Zero Hour executable.

## What it proves

- `W3DDX12LaunchCheckpoint.exe` is a `WIN32` GUI target compiled together
  with the actual DX12 `Renderer` source and the B03/B04 presentation library.
  It does **not** link the Generals engine, `DX8Backend.cpp`, `d3d8.lib`,
  `d3dx8.lib`, or any D3D8 wrapper.
- `Renderer::Init` receives a real **top-level, game-style HWND**, owned by
  the developer application. It does not create a secondary DX12 preview
  window or execute the retail game's unported W3D UI/asset/geometry paths.
- Native `Renderer::Clear` → `Begin_Scene` → `End_Scene(true)` runs every
  visible frame and gradually changes the clear color, demonstrating that
  D3D12 owns presentation. Moving/resizing and minimizing/restoring the
  window use B04's native client-area tracking.
- Before, during and after frame presentation, the launcher asserts that
  neither `d3d8.dll` nor `d3dx8.dll` was loaded in the process.
- CI builds the actual EXE as **Win32 and x64**, runs 12 WARP frames through
  Windows CTest, and audits the **built PE import and delay-import tables**
  using `scripts/check_dx12_launch_pe.py`. The PE must directly import
  `d3d12.dll` and `dxgi.dll`, without importing D3D8/D3DX8.
  Existing WARP render-probe and GPU pixel-readback tests are retained.

## Build and run (Visual Studio 2022)

From the repository root in PowerShell:

~~~powershell
cmake -S Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/Presentation `
      -B build/w3d-dx12-checkpoint -G "Visual Studio 17 2022" -A Win32
cmake --build build/w3d-dx12-checkpoint --config Release
# Open a native clear-screen window (default hardware adapter):
.\build\w3d-dx12-checkpoint\Release\W3DDX12LaunchCheckpoint.exe
# Optional WARP/software fallback for machines without a D3D12 GPU:
.\build\w3d-dx12-checkpoint\Release\W3DDX12LaunchCheckpoint.exe --warp
# Exit successfully after twelve present calls (useful for automated testing):
.\build\w3d-dx12-checkpoint\Release\W3DDX12LaunchCheckpoint.exe --warp --frames 12 --ci
# Execute all tests, including primary-HWND GPU pixel readback and B10 launch:
ctest --test-dir build/w3d-dx12-checkpoint -C Release --output-on-failure
~~~

Use **Escape** or the close button to exit interactive mode. The window title
clearly identifies this as a no-gameplay checkpoint. Nonzero exit status means
native initialization, adapter access, GPU-frame submission, or import
invariants failed; CI never suppresses these failures.

### Downloadable validation builds

GitHub Actions workflow
[DX12 W3D Shader Validation](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/workflows/dx12-shader-validation.yml)
uploads `w3d-dx12-b10-launch-checkpoint` with Win32 and x64 executables and
this README. **Win32** matches the current Generals/Zero Hour game target;
**x64** is for cross-architecture validation. These are **developer tools**,
not replacements for the game binaries; running them does not require the
Generals asset archives or game directory.

## Fail-closed separation

`W3D_DX12_DEVELOPER_CHECKPOINT` is defined **only** on this standalone
executable target. It bypasses B04's `W3D_DX12_B04_DIAGNOSTIC` environment
gate solely inside the separately linked developer process. The actual
`W3D_RENDER_BACKEND=DX12` Generals and Zero Hour game builds still fail
closed when their W3D asset and UI paths attempt to launch normally. Do
**not** set this macro on the game's `corei_ww3d2` target or treat this
checkpoint as evidence that gameplay can render without DX8.

**B10 is an isolated render-backend launch success, not gameplay parity.**
B05 pass scheduling, B06 robust device/mode policy, B07 and R01–R04
authoritative W3D resources, M01 scene draw submission, and the complete
DX12-only game path remain outstanding.
