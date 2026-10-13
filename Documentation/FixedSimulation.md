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

## Validation

The standalone fixed-step clock was compiled with a C++11 compiler and exercised at rendering rates of 5, 10, 20, 24, 30, 60, 120, 144 and 240 FPS over 10 simulated seconds. Each case advanced 300 simulation ticks. The repository GoogleTest suite contains matching tests for timing, catch-up, reset and frequency changes.

**Not yet verified**: Windows build of the integrated code, in-game gameplay, offline/replay CRC equivalence, visual interpolation, animations, multiplayer compatibility and save-load behavior. CI may not run automatically for a workflow introduced only on a feature branch; inspect Actions and run the Windows build/tests explicitly if required.

## Risks and remaining scope

- W3D visual animation updates and unit translation interpolation may need further work for truly smooth high-FPS presentation.
- The historic single-tick network cadence is intentionally preserved pending lockstep tests.
- Pausing, loading, replay fast-forward and CPU overload need direct gameplay testing.
- Tests on a non-Windows compiler validate the isolated scheduler, **not** the entire engine.
- The source was AI-assisted and requires human review prior to merge.
