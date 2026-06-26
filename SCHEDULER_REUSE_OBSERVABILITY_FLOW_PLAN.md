# Scheduler / Reuse Observability Flow Plan

## Objective

Document the passive hierarchy that now exists around scheduler-side telemetry,
track producer state, and reuse observability before any new live runtime retry.

This document is intentionally preparatory.

It does not introduce runtime ownership changes.

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

## Passive hierarchy

The current passive hierarchy is:

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
- keep simulation and track reuse surfaces symmetric

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

Derived compile-only helper for future HUD/debug use:

- packet: `src/game_loop_scheduler_reuse_debug_telemetry_contracts.hpp`
- assembler: `src/game_loop_scheduler_reuse_debug_telemetry_assembler.hpp`
- source boundary: `SchedulerReuseFlowObservabilityPacket`

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

## Rules for the first future live retry

The first live retry using this hierarchy must:

1. consume an already existing passive packet
2. remove equivalent local logic in the same patch
3. stay in one call site only
4. avoid changing scheduling policy
5. keep ISO at `4134912`

Good first candidate shapes:

- debug/presenter-only read of `SchedulerReuseObservabilityPacket`
- local observability assembly point that replaces equivalent scattered reads

Bad first candidate shapes:

- changing dispatch/drain policy
- changing producer kick/fallback logic
- mixing scheduler/reuse aggregation with audio, render, or bootstrap work

## Validation hooks

Compile-only SH2 validation:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

Stable build validation:

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Related documents

- `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
- `PASSIVE_TO_RUNTIME_INTEGRATION_PLAN.md`
- `SIMULATION_SCHEDULER_PLAN.md`
