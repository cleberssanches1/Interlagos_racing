# SH2 Balancing Execution Roadmap

## Objective

Finish the Master/Slave SH2 balancing refactor without losing stable boot.

The goal is not simply to move work to the Slave SH2.
The real goal is:

1. reduce `simMasterWaitTicks`;
2. reduce track-side waits in lockstep paths;
3. keep the Master focused on final render, HUD, audio and critical IO;
4. prepare the project for real work overlap between both SH2 CPUs.

## Current stable baseline

### Master SH2

- frame orchestration
- input
- camera
- background
- HUD
- final VDP submission
- PCM/SGL audio driver calls

### Slave SH2

- gameplay/car simulation in lockstep
- track producer/sort in the current guarded pipeline

### Dominant bottleneck

- the Master already dispatches work to the Slave;
- but it still blocks within the same frame on critical paths:
  - car/gameplay simulation;
  - track preparation barriers.

Result:

- there is execution parallelism;
- there is not yet consistent latency gain.

## Technical end state

### Master responsibilities

- input
- packet assembly
- consume already prepared snapshots
- camera
- HUD
- final render
- real audio driver calls

### Slave responsibilities

- gameplay/car physics
- car render preparation
- track planning/producer/sort
- preparation of data that can be consumed on the next frame

## Current operating rules

Before any new change in the critical runtime:

1. `src/game_loop_system.hpp` only grows if the same patch removes equivalent code elsewhere;
2. do not reintroduce `N-1` car simulation now;
3. keep audio, HUD, VDP and input on the Master SH2;
4. always validate with a clean build;
5. the ISO must remain exactly `4134912` bytes.

Validation script:

- `tools/validate_saturn_stable_build.ps1`

Usage:

- `powershell -ExecutionPolicy Bypass -File tools/validate_saturn_stable_build.ps1`
- `powershell -ExecutionPolicy Bypass -File tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Recommended execution order

### Stage 1 - hold `SimulationScheduler` stable

**Priority:** maximum  
**Risk:** medium/high  
**Sensitive file:** `src/game_loop_system.hpp`

Current rule for this stage:

- keep simulation lockstep as the stable baseline;
- do not try to cache telemetry inside `GameLoopSystem` right now;
- any future wait reduction must come first from external preparation and measurement, not from extra runtime state inside the loop.

Acceptance criteria:

- stable build;
- ISO at `4134912` bytes;
- no telemetry regression;
- stable emulator boot.

### Stage 2 - isolate `CarRenderSystem`

**Priority:** high  
**Risk:** medium  
**Target files:** `src/car_system.*`, `src/mesh_renderer.*`

Goal:

- keep gameplay and car control cohesive in `CarSystem`;
- move only passive visual preparation toward explicit render contracts.

What should leave `CarSystem` over time:

- render position preparation;
- final visual yaw composition;
- visual-only transient state;
- explicit render packet assembly.

What must stay in `CarSystem`:

- input and car command flow;
- gameplay facade;
- drivetrain/debug snapshots;
- anything required for coherent audio/gameplay in the current frame.

Acceptance criteria:

- car remains visually identical;
- `GameLoopSystem` does not grow;
- `CarSystem` loses visual-only responsibilities without changing runtime behavior.

Passive groundwork already available:

- `src/car_render_contracts.hpp`
- `src/car_render_state_assembler.hpp`
- `src/car_shadow_assembler.hpp`
- `src/car_render_submitter.hpp`
- `src/car_render_transition_ops.hpp`
- `src/car_render_system.hpp`
- `src/game_loop_car_visual_packet.hpp`
- `src/game_loop_car_visual_packet_assembler.hpp`
- `CAR_RENDER_PASSIVE_FLOW_PLAN.md`

Current execution rule for this stage:

- do not add `CarRender` packet assembly into `src/game_loop_system.hpp`
  unless the same patch removes equivalent visual logic first
- the current ISO envelope proves that additive integration is not acceptable
- treat `CAR_RENDER_PASSIVE_FLOW_PLAN.md` as the substitution map for the next
  sector-neutral runtime step
- do not isolate `SubmitCarRender(...)` telemetry as the next runtime cut;
  it is too small to pay for passive packet integration by itself
- prefer the shadow-preparation slice as the next runtime candidate because it
  contains duplicated anchor/yaw setup that can be removed and replaced in the
  same patch

### Stage 3 - isolate `TrackRenderScheduler`

**Priority:** high  
**Risk:** medium/high  
**Target files:** `src/track_system.*`, `src/track_draw_producer.hpp`

Safe sequence for this stage:

1. keep passive contracts outside runtime first;
2. document the explicit track packet before consuming it;
3. measure each telemetry bridge separately;
4. only then attempt controlled `N-1` visual reuse for track data;
5. never mix in one patch:
   - scheduler runtime changes;
   - cached telemetry state;
   - a new reuse decision path.

Acceptance criteria:

- synchronous fallback still exists;
- track path remains stable;
- safe mode remains valid;
- previous-frame draw list reuse is explicit and reversible.

### Stage 4 - finish `CdAssetSystem`

**Priority:** medium  
**Risk:** low/medium  
**Target files:** `src/main.cxx` and CD loaders

Goal:

- centralize CD read jobs;
- centralize retries;
- make staging explicit and observable.

Passive groundwork already available for this stage:

- `src/cd_asset_contracts.hpp`
- `src/cd_asset_parse_assembler.hpp`
- `src/cd_asset_transition_ops.hpp`
- `src/cd_asset_system.hpp`
- `src/game_loop_cd_asset_packet.hpp`
- `src/game_loop_cd_asset_packet_assembler.hpp`
- `CD_ASSET_PASSIVE_FLOW_PLAN.md`

Acceptance criteria:

- `main.cxx` stops orchestrating raw CD asset loading directly;
- CD jobs become inspectable;
- boot remains stable.

Current execution rule for this CD stage:

- do not touch boot ordering yet
- do not introduce background CD jobs yet
- prefer first cuts that only centralize SBA/anchor request metadata and
  telemetry, not read timing

### Stage 5 - finish `MemoryBudgetSystem`

**Priority:** medium  
**Risk:** medium  
**Target files:** bootstrap, track, audio and loaders

Minimum categories:

- track
- car
- audio
- HUD
- CD staging

Acceptance criteria:

- memory decisions stop being scattered;
- footprint per category becomes predictable;
- later changes stop breaking because of implicit allocation drift.

### Stage 6 - only then revisit lockstep removal

**Priority:** maximum  
**Risk:** high  
**Prerequisites:** stages 1, 2 and 3 complete

Goal:

- reduce same-frame Master waits only after explicit packets and safe fallbacks are already stable.

## Recommended sequence from the current state

### Phase A - guard rails and baseline

- keep the current runtime unchanged;
- validate every step with `tools/validate_saturn_stable_build.ps1`;
- record ISO size, boot status and basic telemetry as mandatory gates.

### Phase B - passive cuts with zero runtime cost

- continue extracting small passive helpers from `src/game_loop_system.hpp`;
- prioritize overlay, snapshots and visual packet contracts;
- avoid adding any new member state to `GameLoopSystem`.

Current passive observability slice already prepared:

- `src/game_loop_presentation_ops.hpp`
- `src/game_loop_overlay_contracts.hpp`
- `src/game_loop_overlay_state_assembler.hpp`
- `src/game_loop_telemetry_contracts.hpp`
- `src/game_loop_telemetry_state_assembler.hpp`
- `src/game_loop_memory_presentation_contracts.hpp`
- `src/game_loop_memory_presentation_state_assembler.hpp`
- `src/game_loop_observability_contracts.hpp`
- `src/game_loop_observability_state_assembler.hpp`

Rule for the next safe runtime step:

- only introduce stack-local packet assembly in `GameLoopSystem`;
- do not replace print behavior in the same patch;
- do not mix this with scheduler, audio, drivetrain or render ownership changes.

### Phase C - prepare track-side overlap

- finish the passive track render packet contract;
- make explicit what the Master consumes from track work;
- prepare reuse/latency measurements outside the critical loop.

Passive groundwork now available for this stage:

- `src/track_render_contracts.hpp`
- `src/track_render_state_assembler.hpp`
- `src/track_render_telemetry_assembler.hpp`
- `src/track_render_transition_ops.hpp`
- `src/track_render_scheduler.hpp`
- `src/game_loop_track_render_packet.hpp`
- `src/game_loop_track_render_packet_assembler.hpp`
- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`

Current execution rule for this track stage:

- do not touch producer/sort ownership yet
- do not introduce `N-1` reuse yet
- prefer first runtime cuts that only share one local telemetry snapshot across
  existing overlay/SH2 presentation paths

### Phase D - try conservative wait reduction on track only

- test track-side reductions before touching car simulation;
- apply one micro-step at a time;
- require clean build, ISO validation and emulator validation after each step.

### Phase E - only later reconsider broader async behavior

- only if the track packet path is stable and reversible;
- only if telemetry proves actual Master-side gain;
- never together with audio or drivetrain changes.
