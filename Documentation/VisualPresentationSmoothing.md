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
5. **Visual pose preparation before particles.** After W3D display update
   and before particle simulation, each GameClient walks the drawables and
   prepares W3D models that already own active bone-attached particle
   emitters. This sets the interpolated parent transform, turret angles
   and animated bone transforms *before* emission instead of one render
   submission too late. Models without attached emitters retain the
   traditional on-demand update path, avoiding an expensive prepass for
   every visible/offscreen unit. New bone-tracked systems may be created
   during early preparation **only if the model was already visible**;
   previously unseen/offscreen units remain on the ordinary lazy draw path.
   Prepared models are not advanced again in the same presentation frame,
   preventing duplicate animation, recoil or visual-state stepping.
   A drawable caches its exact visual matrix to avoid applying client
   physics twice on the same frame.

6. **Full 3D body rotation.** Each simulation tick captures a rigid world-space
   quaternion for each drawable. Spherical interpolation blends aircraft
   pitch and banking as well as terrain-aligned tilt (in both games), with
   a snap for abrupt >150-degree rotations and long-distance teleports.
   Invalid quaternion samples leave authoritative orientation unchanged.
7. **Camera lock smoothing.** W3DView follows interpolated drawable poses
   in FOLLOW, TETHER, aircraft yaw and 3D model-tracking paths. Its easing
   coefficients now account for actual presentation frame delta, and the
   follow acceleration is per camera rather than a shared static variable.

## Single-player timing and overlay pass

- Recoil timing uses elapsed presentation time rather than one authored step
  per visible render. Render modules are guarded from multiple state updates
  when multiple passes or views submit the same model in one frame.
- The W3D camera shaker now advances once per presentation in seconds, and
  generates its camera sample only for the final camera transform. Constraint
  computations no longer age or reroll active shake effects.
- Health bars, veterancy, formation marks, ammo/container pips, captions and
  construction progress follow the same interpolated positions as models.
  The object's original geometry offsets and UI values are retained; no
  hit testing or authoritative gameplay transforms are changed.
- This pass targets single-player smoothing. Multiplayer still uses its
  original lockstep gate and non-interpolated render phase.

## Visuals still pending

- WW3D skeletal animation already samples `WW3D::Get_Logic_Time_Milliseconds()`
  (including fractional render time) in `Animatable3DObjClass::Compute_Current_Frame()`.
  It has **not** been forcefully switched to a new clock: animation
  completion, transitions and looping require separate gameplay tests.
- Camera shake still deliberately advances at fixed simulation cadence:
  its high-frequency impulses are not yet smoothed during presentation.
- Animation-slaved cameras and some effects may read last-rendered W3D
  bone transforms. The selective prepass covers existing bone-tracked
  particle emitters, not every render module or effect. Expanding this
  blindly could spawn effects offscreen and alter stateful animation
  transitions.
- Other effect attachment paths that read authoritative Drawable transforms,
  or use ClientUpdate modules outside the W3DModelDraw bone-tracker, may
  still step at 30 Hz. These require separate presentation-only sampling
  after the bone-emitter preparation is tested. Only bone-tracked W3D
  particle systems are addressed by this stage.
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
- Test aircraft banking, altitude changes, and terrain-aligned rotations
  for roll/pitch continuity, including abrupt 180-degree turns.
- Test FOLLOW/TETHER camera locks, selected-unit tracking, flying units,
  destruction and target switching at 30/60/144/240 FPS.
- Evaluate missile launches, muzzle flashes, barrel bones, exhaust and tracks
  attached to turning/moving units for visual drift. In particular, compare
  bone-attached smoke/exhaust on rapidly rotating turrets and moving units,
  including offscreen/reentering units and new particle-system creation.
- Verify `GENERALS_DISABLE_VISUAL_POSE_PREPASS=1` restores the old
  attachment timing without affecting gameplay, world-pose interpolation,
  or simulation frequency; restart between A/B runs.
- Run multiplayer and replay checks. A Windows build/test success alone
  cannot establish lockstep or replay determinism.

For an A/B comparison of world pose interpolation, launch with
`GENERALS_DISABLE_DRAWABLE_INTERPOLATION=1`. This disables body, turret
and selected-unit camera pose interpolation, but not the new render ordering
or time-based scripted camera durations.

For a separate A/B test of the **new particle-attachment pose stage**
(while keeping the established model/turret interpolation), launch with:

```bat
set GENERALS_DISABLE_VISUAL_POSE_PREPASS=1
generalszh.exe
```

Omit the variable, or set it to `0`, to use early pose preparation.
The switch is read once per process; restart between runs.

The code is AI-assisted and intentionally left on an unmerged feature branch.
The repository's AI contribution policy requires human verification and
human-authored PR descriptions before any contribution or merge.
