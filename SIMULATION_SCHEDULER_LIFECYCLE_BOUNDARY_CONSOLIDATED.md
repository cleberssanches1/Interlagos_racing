# Simulation Scheduler Lifecycle Boundary Consolidated

## Objective

Record the current consolidated passive state of the
`SimulationSchedulerLifecycleObservabilityPacket` boundary.

This document is inventory-only.

It does not authorize runtime ownership changes by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the critical runtime host
- scheduler dispatch, drain, and completion ordering remain unchanged
- no live ownership migration is introduced by this boundary

## Consolidated boundary

The lifecycle subflow is split into three passive helper families:

### 1. Narrow runtime views

Files:

- `src/game_loop_simulation_drain_view_contracts.hpp`
- `src/game_loop_simulation_drain_view_assembler.hpp`
- `src/game_loop_simulation_completion_view_contracts.hpp`
- `src/game_loop_simulation_completion_view_assembler.hpp`
- `src/game_loop_simulation_scheduler_telemetry_view_contracts.hpp`
- `src/game_loop_simulation_scheduler_telemetry_view_assembler.hpp`

Role:

- define and assemble the minimal passive views for:
  - drain policy/state
  - completion/in-flight state
  - scheduler telemetry counters/status

### 2. Lifecycle assembly

Files:

- `src/game_loop_simulation_scheduler_lifecycle_observability_contracts.hpp`
- `src/game_loop_simulation_scheduler_lifecycle_observability_assembler.hpp`
- lifecycle compile-only shim removed after smoke validation stopped including it

Role:

- assemble `SimulationSchedulerLifecycleObservabilityPacket`
- expose the compile-only bridge from runtime/domain packets into the lifecycle
  observability packet

### 3. Downstream presentation reuse

Files:

- `src/game_loop_presentation_ops.hpp`

Role:

- consume the lifecycle packet as a passive input to SH2 split telemetry
  snapshot assembly

### 4. Compile-only lifecycle-to-track-render preview

Files:

- preview-contract layer removed after smoke validation stopped depending on it

Role:

- compile-only preview assembler was removed after smoke validation stopped
  depending on it
  - `TrackRenderSh2PresentationPacket`
- keep one passive inspection point that spans scheduler lifecycle state and the
  current SH2 track-render presentation output

## What still stays in the host

The following ownership remains local to `src/game_loop_system.hpp`:

- simulation dispatch ordering
- drain/wait policy decisions
- job completion consumption
- cadence and call-site timing of lifecycle-related presentation

## Functional coverage

The consolidated passive boundary now covers:

- drain packet -> drain view
- completion packet -> completion view
- scheduler telemetry/runtime state -> telemetry view
- view trio -> lifecycle observability packet
- lifecycle packet -> presentation-side SH2 telemetry reuse
- lifecycle packet + track-render telemetry -> preview packet

## Why this boundary is considered consolidated

It now has:

- explicit view contracts
- explicit lifecycle assembly helpers
- explicit compile-only runtime bridge
- explicit compile-only preview above lifecycle + track-render presentation
- host-local ownership preserved
- no live scheduler/runtime ownership migration

## Recommended next moves

Do next:

1. keep this boundary runtime-stable
2. use it as precedent for another narrow Scheduler/Reuse aggregate
3. avoid broad scheduler/reuse live retries until a single-call-site target is clear

Do not do next:

- move scheduler ownership out of the host in one patch
- mix this boundary with audio/render/bootstrap changes
- widen this boundary into a broad scheduler/reuse live aggregate immediately
