# Physics, Movement and Wall-Collision Action Plan

## Objective

Analyze the current car physics runtime to:

1. identify real performance optimization opportunities
2. explain why the car can get stuck on walls
3. define the safest correction path to repel the car from walls without
   destabilizing Saturn runtime
4. identify movement-quality improvements worth doing under the current
   low-cost SH2 profile

## Current active runtime profile

The current build runs with:

- `PHYSICS_V2_ENABLED=1`
- `PHYS_SATURN_LOW_COST=1`
- `PHYS_WALL_COLLISION_RUNTIME=1`

Relevant anchors:

- `src/physics_feature_flags.hpp`
- `Makefile`

Practical interpretation:

- the active car physics path is `VehiclePhysicsV2`
- the active movement profile is the low-cost Saturn path
- wall collision runtime is enabled

## Key findings

## 1. The active movement model is intentionally simplified

In the current build, the low-cost path is not only a cheaper implementation;
it is a different driving model.

Relevant anchors:

- `src/car_physics_v2.hpp`
- `src/car_dynamics_model.hpp`

Important behavior:

- `VehiclePhysicsV2::Step()` enters the low-cost path every frame
- inside `DynamicsModel::IntegratePlanar(...)`, the condition
  `useLowSpeedKinematic || Tunables::kEnableSaturnLowCostPhysics`
  means the current build stays on the arcade/kinematic yaw branch
- that branch zeros or heavily suppresses lateral behavior and avoids the more
  expensive slip-based path

Consequence:

- performance is better
- movement fidelity is lower
- wall interaction is harsher because tangential slide is not preserved well

## 2. The wall query is already optimized, but the wall response is too aggressive

The current low-cost wall handling is split in two stages:

### Stage A - wall push query and planar correction

Relevant anchors:

- `src/car_ground_follower.hpp`
- `src/track_collision_query.hpp`
- `src/track_system.cxx`

Current behavior:

- the ground follower asks for a planar wall push
- the result is cached into:
  - `lastWallPushX`
  - `lastWallPushZ`
  - `lastWallQueryHit`
- that push is added to `correctionX/correctionZ`
- `ApplyVerticalAdhesion(...)` applies that correction to world position

This is already relatively efficient for Saturn because:

- body-clip wall reaction is disabled in low-cost mode
- the low-cost wall query uses only one probe
- the `TrackSystem` hot path uses cached 2D wall segments and mostly int32 math

### Stage B - low-cost wall impact response

Relevant anchors:

- `src/car_physics_v2.hpp`

Current behavior after adhesion:

- if wall push exists:
  - forward speed is strongly damped
  - lateral speed is zeroed
  - yaw rate is damped
  - position is overwritten to:
    - `preStepPosition + lastWallPush`

Consequence:

- the tangential component of the already integrated planar movement is lost
- the car keeps re-entering the same wall normal on the next frame
- the result feels like the car is glued to the wall face

This is the main root cause of the “car stuck on wall” behavior.

## 3. The sticking issue is not mainly in wall detection; it is in wall resolution

`TrackSystem::FindPlanarWallPush(...)` already does:

- swept overlap check
- crossed-plane detection
- penetration resolution against cached 2D wall segments
- local-first scan with global fallback cadence

Relevant anchors:

- `src/track_system.cxx`

That means the larger problem is not “the wall was not found”.

The larger problem is:

- the low-cost runtime reacts by cancelling too much motion
- and by re-anchoring the car to `preStepPosition + push` instead of preserving
  tangential slide

## 4. There are real performance opportunities, but not in the most expensive places first

The wall query path is already reduced significantly for Saturn.

The safer performance targets are:

### A. Remove duplicated policy helpers between `SimpleCarPhysics` and `VehiclePhysicsV2`

The same grip-scale update pattern appears in:

- `src/simple_car_physics.hpp`
- `src/car_physics_v2.hpp`

This is not a frame-time win by itself, but it is a maintainability and code
size win.

### B. Make the low-cost wall response a dedicated helper

Right now the low-cost wall behavior lives inline in `VehiclePhysicsV2::StepOnce(...)`.

Extracting one narrow helper would:

- make the wall response easier to profile
- reduce the chance of accidentally widening the physics hot path
- make future repel tuning safer

### C. Revisit wall-query cadence only after the repel fix

There is room for adaptive cadence under these conditions:

- low speed
- no previous wall hit
- small steering input
- low displacement

But this should happen only after the anti-sticking fix, because current code
explicitly keeps wall refresh active during reused ground probes to avoid
crossing walls on skipped frames.

### D. Keep body-clip planar reaction disabled in low-cost mode

That is already the correct performance choice for Saturn.

Re-enabling it now would likely cost more than it returns.

## Root cause of the wall-sticking bug

The current low-cost wall resolution does all of these at once:

1. damp forward speed strongly
2. zero lateral speed
3. damp yaw rate
4. overwrite planar position from `preStepPosition`
5. keep querying the same wall on the next frame

That combination removes the car’s tangential escape behavior.

Instead of sliding along the wall, the car repeatedly resolves into the wall
normal and loses its side component.

## Correct repel strategy

The safest repel strategy for the current Saturn profile is:

### Rule 1 - remove only inward motion

Do not zero all planar motion.

Instead:

- compute wall normal from `lastWallPushX/Z`
- decompose current planar motion into:
  - inward normal component
  - tangential component
- cancel only the inward component
- keep tangential motion, optionally damped

### Rule 2 - add a small separation bias

Use:

- `resolvedPush + normal * wallSkin`

where `wallSkin` is a small fixed offset.

Goal:

- keep the car slightly outside the wall plane after resolution
- avoid re-penetration due to fixed-point rounding and next-frame integration

### Rule 3 - preserve slide along the wall

After impact:

- forward/tangential motion should survive partially
- only the component pointing into the wall should be removed

That gives:

- natural glancing collisions
- less sticking
- better recovery when steering away from the wall

### Rule 4 - keep the response single-authority

Do not let both of these own the final planar result in conflicting ways:

- `GroundFollower` wall correction
- low-cost impact overwrite from `preStepPosition`

The safer shape is:

- `GroundFollower` continues to detect and provide push
- one explicit low-cost wall-response helper decides the final planar velocity
  and extra separation
- final positional rewrite preserves tangential displacement instead of erasing it

## Movement-quality improvements worth doing

## 1. Improve low-cost wall-slide behavior first

This has the best ratio of:

- player-visible gain
- low runtime risk
- low code-size risk

## 2. Make low-cost yaw/movement less binary at medium/high speed

Today the low-cost build effectively stays in the cheap kinematic branch.

Possible future improvement:

- keep current low-cost behavior at low speed
- add a hybrid medium/high-speed branch with limited tangential slip only above
  a threshold

This should be treated as optional future work, not as the first patch.

## 3. Centralize drivetrain/physics hot-path helpers

The physics code currently mixes:

- RPM behavior
- gearbox transitions
- drag
- steering authority
- wall response
- ground support

The runtime is already functional, but the wall and movement tuning will be
safer if the hot-path response pieces are grouped more clearly.

## Action plan

## Phase 1 - Wall-sticking fix first

Implement the smallest safe runtime fix:

1. extract one narrow low-cost wall-response helper from `VehiclePhysicsV2`
2. compute planar wall normal from `lastWallPushX/Z`
3. preserve tangential displacement
4. cancel only inward motion
5. add a small separation bias after collision
6. keep existing wall query and existing `TrackSystem` wall detection unchanged

Expected result:

- car no longer remains glued to wall faces
- wall hits still block penetration
- glancing collisions slide instead of sticking

## Phase 2 - Performance-safe cleanup around active physics path

After Phase 1 is stable:

1. unify duplicated grip-scale update logic between:
   - `SimpleCarPhysics`
   - `VehiclePhysicsV2`
2. extract the low-cost wall response into a dedicated helper boundary
3. keep body-clip reaction disabled in low-cost mode

Expected result:

- smaller maintenance surface
- lower risk for future tuning
- no behavior change beyond the wall fix

## Phase 3 - Adaptive query tuning only if needed

Only after Phase 1 is validated:

1. profile wall-query frequency at low speed
2. consider adaptive cadence only when:
   - low speed
   - low steering
   - no current wall hit
   - no recent wall hit streak
3. keep full-rate queries when speed or steering rises

Expected result:

- small CPU savings
- lower risk of reintroducing wall tunneling

## Phase 4 - Optional movement-fidelity pass

Treat as a separate goal:

1. evaluate a hybrid low-cost/high-speed lateral model
2. keep current arcade branch at low speed
3. enable limited slip only where it produces clear benefit

Expected result:

- better medium/high-speed feel
- higher realism without committing to the full expensive path

## Recommended execution order

1. Phase 1
2. Validate on emulator
3. Phase 2
4. Re-measure
5. Only then consider Phase 3 or 4

## Validation gate for runtime patches

For each live patch in this subsystem:

1. `powershell -ExecutionPolicy Bypass -File tools\\validate_saturn_stable_build.ps1`
2. recreate:
   - `BuildDrop\\passive_header_validation`
   - `BuildDrop\\observability_header_validation`
3. `powershell -ExecutionPolicy Bypass -File tools\\validate_game_loop_passive_headers.ps1`
4. `powershell -ExecutionPolicy Bypass -File tools\\validate_game_loop_observability_headers.ps1`
5. remove transient objects if needed:
   - `src\\car_wheel_rig.o`
   - `src\\track_pipeline_stages.o`

## Decision

The highest-value next move is not broad physics rewrites.

The highest-value next move is:

- fix low-cost wall response so the car is repelled and can slide away
- then do small structural cleanup around the active physics path
