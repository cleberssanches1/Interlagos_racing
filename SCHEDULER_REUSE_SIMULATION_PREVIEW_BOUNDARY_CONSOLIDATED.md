# Scheduler Reuse Simulation Preview Boundary Consolidated

## Objective

Consolidate the current compile-only boundary that joins the higher
`scheduler/reuse flow` observability packet with the local simulation-side
reuse debug-preview branch.

This document is inventory-only.

It does not authorize runtime ownership changes by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the critical runtime host
- the accepted live seam remains the track-only reuse activation
- the simulation-side reuse branch remains compile-only above that seam

## Boundary hierarchy

Lower input branches:

- `SchedulerReuseFlowObservabilityPacket`
- `SimulationReuseRuntimeDebugPreviewPacket`

Joined preview boundary:

- `SchedulerReuseSimulationPreviewPacket`
- `PresentSchedulerReuseSimulationPreviewPacket(...)`

Higher consumers already prepared above it:

- `PresenterSummarySchedulerReuseSimulationPreviewPacket`
- `PresenterSummarySchedulerReuseSimulationViewPacket`
- `PresenterSummarySchedulerReuseSimulationTextPacket`
- `PresentPresenterSummarySchedulerReuseSimulationTextPacket(...)`

## Files

### Lower observability branch

- `src/game_loop_scheduler_reuse_flow_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_flow_observability_assembler.hpp`

### Lower simulation-side debug branch

- `src/game_loop_simulation_reuse_runtime_debug_preview_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_presenter_ops.hpp`

### Joined scheduler/reuse simulation preview

- `src/game_loop_scheduler_reuse_simulation_preview_contracts.hpp`
- `src/game_loop_scheduler_reuse_simulation_preview_assembler.hpp`
- `src/game_loop_scheduler_reuse_simulation_preview_presenter_ops.hpp`

### Higher presentation-facing consumers

- `src/game_loop_presenter_summary_scheduler_reuse_simulation_preview_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_preview_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_view_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_view_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_presenter_ops.hpp`

## What this boundary proves

- the scheduler/reuse observability hierarchy can already receive the
  simulation-side preview branch without touching runtime ownership
- the simulation-side branch can now be validated structurally above the local
  debug boundary and below presenter/resumo
- future presenter/debug retries can target this joined preview instead of
  rebuilding the chain ad hoc in `src/game_loop_system.hpp`

## Runtime status

- compile-only only
- no live scheduler/reuse aggregate consumption
- no live simulation-side branch consumption
- no lockstep or producer behavior change

## Why this boundary is important

- it is the first explicit passive join between the broad
  `scheduler/reuse flow` hierarchy and the new simulation-side local debug
  branch
- it defines the clean mid-layer attach point between observability and
  presenter/resumo
- it keeps the next future live retry narrower than a direct
  `SchedulerReuseFlowObservabilityPacket` consumer

## Safe next step

The next safe move remains compile-only:

1. keep this boundary documented as the mid-layer join
2. keep presenter/resumo work above this packet
3. only retry runtime later with one remove-first local text/debug consumer

## Guard rails

- keep ISO at `4134912`
- do not consume this packet live before the simulation-side branch has an
  exact remove-first target
- do not mix this boundary with scheduler policy or SH2 ownership changes
- do not reopen the broad combined simulation+track owner seam first

## Related documents

- `SIMULATION_REUSE_RUNTIME_DEBUG_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `PRESENTER_SUMMARY_SCHEDULER_REUSE_SIMULATION_BOUNDARY_CONSOLIDATED.md`
- `PRESENTER_SUMMARY_SCHEDULER_REUSE_SIMULATION_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
