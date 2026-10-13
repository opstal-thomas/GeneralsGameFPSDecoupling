# Fixed-step simulation / variable-rate presentation

This branch introduces a bounded fixed-step clock for Command & Conquer: Generals and Zero Hour.

## Architecture

- Single-player gameplay defaults to **30 whole simulation ticks per second**, independent of rendering FPS. Existing explicit logic time-scale and replay fast-forward options retain separate behavior.
- Rendering, client animation, audio, message translation, mouse, keyboard and UI are serviced once per rendered frame.
- The main loop may execute multiple complete single-player simulation ticks if rendering runs below 30 FPS.
- The scheduler limits a long frame to **8 catch-up ticks** and caps incoming elapsed time at 250 ms to avoid catastrophic spirals.
- Multiplayer deliberately keeps the existing network-authorized update path and command-frame gate. Network catch-up is **not** part of this change.
- FramePacer reports the clock's interpolation alpha to existing render-side interpolators; this does not automatically interpolate translation for every drawable.
- No changes are made to simulation object time steps, replay formats or network command serialization.

## Rendering FPS configuration

The rendering frame rate is intentionally independent of the fixed 30 Hz
simulation rate. This branch now defaults to **144 rendered frames/second**
rather than inheriting the retail 30 FPS cap from GameData.ini.

Edit `Options.ini` in the game's user data directory to override the
presentation limit:

```ini
FPSLimit = yes
RenderFPSLimit = 240
```

`RenderFPSLimit` accepts 30 through 1000. Omit it to default to 144.
Set `FPSLimit = no` to uncap rendering; this does **not** alter
gameplay simulation speed. The Options menu's Unlock FPS checkbox is also
now saved, and switching graphics LOD no longer silently re-enables
the rendering cap. Changes to the numeric limit require a game restart.

The effective output FPS may still be lower due to display VSync,
GPU-driver limits, a monitor's refresh rate or hardware load. Verify using
an external frame-rate counter rather than judging by unit animation alone:
full interpolation of all drawable transforms is a separate concern.

## Diagnosing a 30 FPS gameplay ceiling

The main-loop code does not intentionally switch back to a 30 FPS rendering
limit on entering gameplay. If the measured frame rate falls to 30, distinguish
a hidden cap from a graphics bottleneck or VSync synchronization before changing
simulation timing again.

Use the Windows test executable from this branch. In a Command Prompt opened
in the game's installation folder:

```bat
set GENERALS_FPS_DIAGNOSTICS=1
set GENERALS_DISABLE_VSYNC=1
generalszh.exe
```

Substitute `generals.exe` to test the base game. The environmental overrides
affect only that launched process. `GENERALS_DISABLE_VSYNC=1` requests an
immediate Direct3D8 presentation interval, bypassing the fullscreen driver's
default VSync policy for this diagnostic run. Disable it again if you want
normal synchronization; tearing is expected during this test. GPU-driver
forced VSync and external limiters may still apply.

With `GENERALS_FPS_DIAGNOSTICS=1`, the engine appends diagnostic lines to
`%TEMP%\\GeneralsFPSDiagnostics.log` and sends them to the Windows debug
output. Each second, the **Engine** line reports:

- `Loop`: engine updates per real-time second (presentation iteration rate).
- `Simulation`: fixed simulation ticks per real-time second (target 30).
- `Update`: average time spent processing the client, logic and render.
- `Wait`: average time spent in the frame pacer.
- `RenderCap`, `ActiveCap`, `LimitEnabled`, `InGame`, `Network`: effective settings.

The **D3D** line reports actual presents per second and average blocking time
inside Direct3D's `Present()` call.

Interpretation:

- **Loop about 144, Simulation about 30, Presents about 144:** rendering is
  unlocked; 30 Hz *animations* may still look stepped.
- **Loop about 30, Update about 33ms, Wait near 0ms:** client/gameplay render
  work is consuming the frame, rather than the CPU FPS limiter.
- **Loop about 30, large `AveragePresentBlock`:** examine VSync, display
  refresh rate, the driver and GPU-bound rendering.
- **Loop about 30, large `Wait`, ActiveCap 30:** another path is applying an
  explicit pacing limit and should be investigated with the log.

This branch also clears GameLogic's `hasUpdated` latch once per client
presentation, preventing W3D/particles from treating a series of render-only
iterations as multiple new simulation ticks.

## Validation

The standalone fixed-step clock was compiled with a C++11 compiler and exercised at rendering rates of 5, 10, 20, 24, 30, 60, 120, 144 and 240 FPS over 10 simulated seconds. Each case advanced 300 simulation ticks. The repository GoogleTest suite contains matching tests for timing, catch-up, reset and frequency changes.

**Not yet verified**: Windows build of the integrated code, in-game gameplay, offline/replay CRC equivalence, visual interpolation, animations, multiplayer compatibility and save-load behavior. CI may not run automatically for a workflow introduced only on a feature branch; inspect Actions and run the Windows build/tests explicitly if required.

## Risks and remaining scope

- W3D visual animation updates and unit translation interpolation may need further work for truly smooth high-FPS presentation.
- The historic single-tick network cadence is intentionally preserved pending lockstep tests.
- Pausing, loading, replay fast-forward and CPU overload need direct gameplay testing.
- Tests on a non-Windows compiler validate the isolated scheduler, **not** the entire engine.
- The source was AI-assisted and requires human review prior to merge.
