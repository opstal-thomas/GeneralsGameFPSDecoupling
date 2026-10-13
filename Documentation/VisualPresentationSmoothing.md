# Visual presentation smoothing — feature branch

This work starts from the merged `main` fixed-step simulation implementation.
All new interpolation continues to be **client-side only**. The 30 Hz gameplay
clock, multiplayer lockstep messages, simulation state, collision and save
formats are unchanged.

## Changes in this stage

1. **Simulation before rendering.** `GameClient::update()` keeps input,
   UI and command generation before `MessageStream::propagateMessages()`.
   `GameClient::presentFrame()` now performs terrain visual updates,
   W3D display update, particle simulation and display draw **after**
   the fixed simulation ticks and after `FramePacer::setLogicFramePhase()`.
   This avoids consuming the previous render's interpolation alpha.
   The intro still renders in its own special path.
2. **Camera tracking.** Zero Hour's selected-unit tracking consumes the same
   interpolated position as the model. The camera is updated before W3DView
   prepares and renders the current frame.
3. **Turret interpolation.** A new optional `DrawModule` hook captures
   turret yaw/pitch snapshots at the end of each gameplay tick.
   `W3DModelDraw` interpolates shortest-arc turret rotations when drawing.
   History resets on object rebind, model-state change and save-game load.
   No turret AI state is modified by presentation.
4. **Scripted camera movement.** Script-authored camera rotation, zoom and
   pitch durations still use their historical 30 Hz frame counts, but
   progress through them with fractional elapsed frames so presentation
   refresh rates (60/144/240 FPS) do not change their real-world duration.

## Visuals still pending

- WW3D skeletal animation already samples `WW3D::Get_Logic_Time_Milliseconds()`
  (including fractional render time) in `Animatable3DObjClass::Compute_Current_Frame()`.
  It has **not** been forcefully switched to a new clock: animation
  completion, transitions and looping require separate gameplay tests.
- Body pitch/roll interpolation and slope-aware full-3D orientation still need
  quaternion interpolation; only translation and flat-ground yaw are blended.
- Scripted camera object-lock following and camera shake still contain
  render-frame-dependent smoothing and should be audited separately.
- Particle emitters and bone attachments can still be one presentation frame
  behind when an effect is simulated before `W3DModelDraw::doDrawModule()`
  applies the latest interpolated model/turret transform.
- Unit selection overlays and FX which read authoritative positions still
  need a consistent policy for consuming visual poses.

## Acceptance checks

Use a separate test game directory and compare to the merged `main`
build using the same map and units:

- At 30/60/144/240 FPS, `Simulation` remains ~30/s in normal skirmish
  and the game remains responsive after match startup.
- Infantry/vehicle translation and turrets rotate smoothly without aim jumps.
- The selected-unit camera follows the unit without snapping back to 30 Hz
  positions; test selection changes and object destruction.
- Scripted zoom, pitch and rotate sequences take the same *real-world*
  duration at each tested rendering FPS.
- Teleports, spawning, save/load, pause/resume and replay fast-forward do not
  smear positions or leave turret poses stale.
- Evaluate missile launches, muzzle flashes, barrel bones, exhaust and tracks
  attached to turning/moving units for visual drift.
- Run multiplayer and replay checks. A Windows build/test success alone
  cannot establish lockstep or replay determinism.

For an A/B comparison of world pose interpolation, launch with
`GENERALS_DISABLE_DRAWABLE_INTERPOLATION=1`. This disables body, turret
and selected-unit camera pose interpolation, but not the new render ordering
or time-based scripted camera durations.

The code is AI-assisted and intentionally left on an unmerged feature branch.
The repository's AI contribution policy requires human verification and
human-authored PR descriptions before any contribution or merge.
