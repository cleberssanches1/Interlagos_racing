# Scheduler / Reuse Observability Flow Plan

## Objective

Document the hierarchy that now exists around scheduler-side telemetry, track
producer state, and reuse observability, including the newly accepted low live
track-only reuse seam.

This document remains primarily structural.

It does not authorize broader runtime ownership changes by itself.

## Stable baseline

- reference ISO: `4134912`
- emulator boot must remain stable
- no new live scheduler/reuse cut should happen without remove-first discipline

## Why this flow exists

The scheduler/reuse area is structurally sensitive because it sits close to:

- Master/Slave orchestration
- lockstep vs async simulation behavior
- track producer in-flight state
- future `N-1` reuse decisions

Even small additive changes in this area can destabilize the emulator boot or
shift binary layout in critical files.

So the safe strategy is:

1. narrow the data surfaces first
2. aggregate them passively
3. document the hierarchy explicitly
4. only later consider a substitutional runtime consumer

## Hierarchy

The current hierarchy is:

### Level 0 - source origin and runtime-owner staging

#### `FrameReuseRuntimeOwnerPacket`

Purpose:

- define the narrow runtime-side owner packet that can live next to real reuse
  producer/history ownership
- stage raw reuse runtime state before it is narrowed again into observability
  source capture

Files:

- `src/frame_reuse_runtime_owner_contracts.hpp`
- `src/frame_reuse_runtime_owner_assembler.hpp`
- `src/frame_reuse_runtime_observability_source_assembler.hpp`
- `src/frame_reuse_runtime_observability_owner_assembler.hpp`
- `src/game_loop_reuse_runtime_source_assembler.hpp`
- `src/game_loop_reuse_runtime_owner_assembler.hpp`
- `src/frame_reuse_observability_capture_ops.hpp`

#### `ReuseObservabilitySourceSnapshot`

Purpose:

- define the domain-level snapshot for future real `frame_reuse`
  ownership outside `src/game_loop_system.hpp`
- let a non-critical future owner snapshot raw:
  - `SimulationReuseDecisionPacket`
  - `TrackReuseDecisionPacket`
  - `FrameReuseTelemetry`
- keep the first real source handoff out of the observability host seam

Files:

- `src/frame_reuse_observability_source_contracts.hpp`
- `src/frame_reuse_observability_source_assembler.hpp`
- `src/frame_reuse_observability_capture_ops.hpp`

#### `ReuseObservabilitySourceOwnerPacket`

Purpose:

- define the frame-reuse-domain owner boundary that can become the future real
  non-critical origin for reuse observability
- keep ownership of the raw source snapshot out of
  `src/game_loop_system.hpp` and out of the observability-domain adapters

Files:

- `src/frame_reuse_observability_source_owner_contracts.hpp`
- `src/frame_reuse_observability_source_owner_assembler.hpp`
- `src/frame_reuse_observability_capture_ops.hpp`

#### `ReuseObservabilitySourcePacket`

Purpose:

- define the future owned source boundary for `frame_reuse` data outside
  `src/game_loop_system.hpp`
- allow a non-critical future owner to snapshot:
  - `SimulationReuseDecisionPacket`
  - `TrackReuseDecisionPacket`
  - `FrameReuseTelemetry`
- avoid the first real runtime source being introduced as direct pointer walking
  across multiple owners inside the critical loop

Files:

- `src/game_loop_reuse_source_state_contracts.hpp`
- `src/game_loop_reuse_source_state_assembler.hpp`
- `src/game_loop_reuse_source_owner_contracts.hpp`
- `src/game_loop_reuse_source_owner_assembler.hpp`

Bridge to the already prepared host seam:

- `FrameReuseRuntimeOwnerPacket`
  -> `FrameReuseDomain::ReuseObservabilitySourceSnapshot`
- `FrameReuseRuntimeOwnerPacket`
  -> `FrameReuseDomain::ReuseObservabilitySourceOwnerPacket`
- `FrameReuseRuntimeOwnerPacket`
  -> `GameLoopObservabilityDomain::ReuseObservabilitySourcePacket`
- `ReuseObservabilitySourceSnapshot`
  -> `FrameReuseDomain::ReuseObservabilitySourceOwnerPacket`
- `FrameReuseDomain::ReuseObservabilitySourceOwnerPacket`
  -> `GameLoopObservabilityDomain::ReuseObservabilitySourceOwnerPacket`
- `GameLoopObservabilityDomain::ReuseObservabilitySourceOwnerPacket`
  -> `ReuseObservabilitySourcePacket`
- `ReuseObservabilitySourcePacket`
  -> `ReuseObservabilitySourceState`
  -> `ReuseObservabilityAssemblyInputs`
  -> `ReuseObservabilityPacket`

### Level 1 - narrow raw views

#### `SimulationDrainViewPacket`

Purpose:

- expose only the scheduler drain policy snapshot relevant to future
  observability or lifecycle-focused consumers

Main fields:

- `mandatoryWait`
- `softSpinLimit`
- `hardSpinLimit`

Files:

- `src/game_loop_simulation_drain_view_contracts.hpp`
- `src/game_loop_simulation_drain_view_assembler.hpp`

#### `SimulationCompletionViewPacket`

Purpose:

- expose only the scheduler completion state relevant to future lifecycle/debug
  consumers

Main fields:

- `jobInFlight`
- `hasCompleted`
- `inFlightIdx`
- `completedIdx`

Files:

- `src/game_loop_simulation_completion_view_contracts.hpp`
- `src/game_loop_simulation_completion_view_assembler.hpp`

#### `SimulationSchedulerTelemetryViewPacket`

Purpose:

- expose only the simulation scheduler counters and status that are relevant to
  high-level scheduling/debug reasoning

Main fields:

- dispatch counts
- track-busy skips
- backoff skips
- drain timeouts / hard waits
- master wait ticks
- slave job ticks
- backoff frame count
- `jobInFlight`
- `hasCompleted`

Files:

- `src/game_loop_simulation_scheduler_telemetry_view_contracts.hpp`
- `src/game_loop_simulation_scheduler_telemetry_view_assembler.hpp`

#### `TrackRenderProducerStatePacket`

Purpose:

- isolate producer-side runtime state from the broader track telemetry view

Main fields:

- `producerJobInFlight`
- `producerSafeModeActive`

Files:

- `src/game_loop_track_render_producer_state_contracts.hpp`
- `src/game_loop_track_render_producer_state_assembler.hpp`

#### Reuse narrow views

Simulation side:

- `SimulationReuseDecisionViewPacket`
- `SimulationReuseTelemetryViewPacket`

Track side:

- `TrackReuseDecisionViewPacket`
- `TrackReuseTelemetryViewPacket`

Purpose:

- split final reuse decisions from accumulated reuse counters
- keep simulation and track reuse surfaces symmetric above the accepted low
  live track-only seam

Files:

- `src/game_loop_simulation_reuse_decision_view_contracts.hpp`
- `src/game_loop_simulation_reuse_decision_view_assembler.hpp`
- `src/game_loop_simulation_reuse_telemetry_view_contracts.hpp`
- `src/game_loop_simulation_reuse_telemetry_view_assembler.hpp`
- `src/game_loop_track_reuse_decision_view_contracts.hpp`
- `src/game_loop_track_reuse_decision_view_assembler.hpp`
- `src/game_loop_track_reuse_telemetry_view_contracts.hpp`
- `src/game_loop_track_reuse_telemetry_view_assembler.hpp`

### Level 2 - reuse family aggregate

#### `ReuseObservabilityPacket`

Purpose:

- group all narrow reuse views into one passive observability bundle

Contents:

- simulation decision view
- simulation telemetry view
- track decision view
- track telemetry view

Files:

- `src/game_loop_reuse_observability_contracts.hpp`
- `src/game_loop_reuse_observability_assembler.hpp`

Derived compile-only debug view:

- `src/game_loop_reuse_observability_debug_contracts.hpp`
- `src/game_loop_reuse_observability_debug_assembler.hpp`

Derived compile-only presenter helper:

- `src/game_loop_reuse_observability_debug_presenter_ops.hpp`

Derived local bundle for Boundary D:

- `src/game_loop_reuse_observability_debug_bundle_contracts.hpp`
- `src/game_loop_reuse_observability_debug_bundle_assembler.hpp`

Derived local bundle presenter:

- `src/game_loop_reuse_observability_debug_bundle_presenter_ops.hpp`

### Level 3 - scheduler lifecycle aggregate

#### `SimulationSchedulerLifecycleObservabilityPacket`

Purpose:

- group drain view, completion view, and scheduler telemetry view into one
  passive lifecycle-oriented scheduler bundle

Contents:

- `SimulationDrainViewPacket`
- `SimulationCompletionViewPacket`
- `SimulationSchedulerTelemetryViewPacket`

Files:

- `src/game_loop_simulation_scheduler_lifecycle_observability_contracts.hpp`
- `src/game_loop_simulation_scheduler_lifecycle_observability_assembler.hpp`

### Level 4 - scheduler/reuse aggregate

#### `SchedulerReuseObservabilityPacket`

Purpose:

- join scheduler-side telemetry with producer state and the full reuse bundle
- create one higher-level passive input for future debug/presenter consumers

Contents:

- `SimulationSchedulerTelemetryViewPacket`
- `TrackRenderProducerStatePacket`
- `ReuseObservabilityPacket`

Files:

- `src/game_loop_scheduler_reuse_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_observability_assembler.hpp`
- `src/game_loop_scheduler_reuse_runtime_debug_bridge_assembler.hpp`

### Level 5 - scheduler/reuse flow aggregate

#### `SchedulerReuseFlowObservabilityPacket`

Purpose:

- provide one highest passive bundle for future boundaries that need scheduler
  lifecycle state together with producer/reuse observability
- avoid recomposing lifecycle and reuse families ad hoc at future live call
  sites

Contents:

- `SimulationSchedulerLifecycleObservabilityPacket`
- `SchedulerReuseObservabilityPacket`

Files:

- `src/game_loop_scheduler_reuse_flow_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_flow_observability_assembler.hpp`
- `src/game_loop_scheduler_reuse_runtime_debug_bridge_assembler.hpp`

Derived compile-only helper for future HUD/debug use:

- packet: `src/game_loop_scheduler_reuse_debug_telemetry_contracts.hpp`
- contract-only leaf remains: `src/game_loop_scheduler_reuse_debug_telemetry_contracts.hpp`
- source boundary: `SchedulerReuseFlowObservabilityPacket`
- current payload shape: valid-only marker

Current compile-only preview above that helper:

- preview-contract layer removed after smoke validation stopped depending on it

Additional compile-only chain helper:

- former bridge leaf `src/game_loop_scheduler_reuse_observability_assembly_ops.hpp`
  later removed after smoke validation stopped depending on it

Current structural effect:

- assembles `ReuseObservabilityPacket`
- exposes reuse source/assembly contracts
- isolates reuse packet assembly from reuse debug-bundle assembly
- exposes a compile-only bridge from scheduler runtime packets into
  `SimulationSchedulerLifecycleObservabilityPacket`
- exposes a compile-only bridge from scheduler telemetry + explicit producer
  flags + `FrameReuseRuntimeOwnerPacket` into
  `SchedulerReuseObservabilityPacket`
- exposes a compile-only bridge from lifecycle + scheduler telemetry + explicit
  producer flags + `FrameReuseRuntimeOwnerPacket` into
  `SchedulerReuseFlowObservabilityPacket`
- assembles `SimulationSchedulerLifecycleObservabilityPacket`
- assembles `SchedulerReuseObservabilityPacket`
- assembles `SchedulerReuseFlowObservabilityPacket`
- assembles `SchedulerReuseDebugTelemetryPacket`
- assembles `SchedulerReusePreviewPacket`

from one stack-local input bundle without touching the broader critical host
path beyond the already accepted narrow seam

## Current live cut below the aggregate hierarchy

One low live reuse cut now exists below the broader symmetric reuse aggregate:

- `src/game_loop_system.hpp` now keeps one narrow
  `GameLoopRuntime::TrackReuseRuntimeState`
- `RenderTrackFrame(...)` captures request-side track reuse inputs and commits
  track history locally
- `TryBuildReuseObservabilityDebugBundle(...)` now builds one real track-only
  `FrameReuseRuntimeOwnerPacket` on demand
- simulation-side reuse remains neutral in that seam
- cumulative reuse telemetry remains outside the live path

This means:

- the hierarchy above remains valid
- but the lowest track-side source activation is no longer purely compile-only
- the next symmetric retry should add only the missing simulation-side branch,
  not restart the full reuse boundary from scratch

The exact missing branch at that seam is documented in:

- `SIMULATION_REUSE_SEAM_COMPLETION_PLAN.md`

## Hierarchy summary

Short form:

1. drain view
2. completion view
3. scheduler telemetry view
4. producer state
5. reuse views
6. reuse observability
7. scheduler lifecycle observability
8. scheduler/reuse observability
9. scheduler/reuse flow observability

Expanded dependency chain:

- `SimulationDrainPacket`
  - ? `SimulationDrainViewPacket`
- `SimulationCompletionPacket`
  - ? `SimulationCompletionViewPacket`
- `SimulationSchedulerTelemetry`
  - ? `SimulationSchedulerTelemetryViewPacket`
- `TrackRenderTelemetryViewPacket`
  - ? `TrackRenderProducerStatePacket`
- `SimulationReuseDecisionPacket`
  - ? `SimulationReuseDecisionViewPacket`
- `FrameReuseTelemetry`
  - ? `SimulationReuseTelemetryViewPacket`
- `TrackReuseDecisionPacket`
  - ? `TrackReuseDecisionViewPacket`
- `FrameReuseTelemetry`
  - ? `TrackReuseTelemetryViewPacket`
- `ReuseObservabilitySourceState`
  - ? `ReuseObservabilityAssemblyInputs`
- narrow reuse views
  - ? `ReuseObservabilityPacket`
- drain view + completion view + scheduler telemetry view
  - ? `SimulationSchedulerLifecycleObservabilityPacket`
- scheduler telemetry view + producer state + reuse bundle
  - ? `SchedulerReuseObservabilityPacket`
- scheduler lifecycle observability + scheduler/reuse observability
  - ? `SchedulerReuseFlowObservabilityPacket`

## What this hierarchy is for

This hierarchy exists to make future runtime cuts:

- narrower
- symmetric
- substitutional
- easier to rollback

It also allows future consumers to choose the correct layer:

- need only counters/status → use a narrow view
- need only reuse behavior → use `ReuseObservabilityPacket`
- need scheduler + reuse reasoning together → use
  `SchedulerReuseObservabilityPacket`

## What must remain unchanged

Before any live retry, keep all of this unchanged:

- simulation dispatch order
- lockstep drain behavior
- track producer scheduling
- safe mode behavior
- `N-1` reuse policy ownership
- current Master/Slave orchestration

## Rules for the next future live retry

The next broad symmetric live retry using this hierarchy must:

1. consume an already existing passive packet
2. remove equivalent local logic in the same patch
3. stay in one call site only
4. avoid changing scheduling policy
5. keep ISO at `4134912`

Good next candidate shapes:

- one local completion of the symmetric simulation-side branch in the same
  reuse seam
- only after that, a debug/presenter-only read of `ReuseObservabilityPacket`
  with equivalent local simulation-side reads removed

Bad first candidate shapes:

- changing dispatch/drain policy
- changing producer kick/fallback logic
- mixing scheduler/reuse aggregation with audio, render, or bootstrap work

## Current accepted envelope/result

- stable build remains `4134912`
- the code-side retry still consumed the known `4096`-byte budget
- the inert ISO pad was reduced to preserve the stable final envelope

## Validation hooks

Compile-only SH2 validation:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

Stable build validation:

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Related documents

- `SIMULATION_REUSE_RUNTIME_DEBUG_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SCHEDULER_REUSE_SIMULATION_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
- `PASSIVE_TO_RUNTIME_INTEGRATION_PLAN.md`
- `SIMULATION_SCHEDULER_PLAN.md`
- `SCHEDULER_TRACK_RENDER_REUSE_FLOW_CONSOLIDATED.md`
