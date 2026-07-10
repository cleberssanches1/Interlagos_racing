# Presenter Summary Scheduler/Reuse Simulation Boundary Consolidated

## Objective

Consolidate the current compile-only presenter/resumo boundary that now carries
the new `scheduler/reuse + simulation reuse preview` chain, without reopening
runtime ownership in `src/game_loop_system.hpp`.

This document is inventory-only.

It does not authorize runtime ownership changes by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the critical runtime host
- the accepted live seam remains track-only at
  `TryBuildReuseObservabilityDebugBundle(...)`
- simulation-side reuse remains compile-only above that seam

## Boundary hierarchy

Lowest local simulation-side debug boundary:

- `SimulationReuseRuntimeDebugPreviewPacket`
- `PresentSimulationReuseRuntimeDebugPreviewPacket(...)`

Higher scheduler/reuse compile-only aggregate:

- `SchedulerReuseSimulationPreviewPacket`
- `PresentSchedulerReuseSimulationPreviewPacket(...)`

Presenter/resumo aggregate above that:

- `PresenterSummarySchedulerReuseSimulationPreviewPacket`

Current narrowed presentation-facing branch above that:

- `PresenterSummarySchedulerReuseSimulationViewPacket`
- `PresenterSummarySchedulerReuseSimulationTextPacket`
- `PresentPresenterSummarySchedulerReuseSimulationTextPacket(...)`

## Files

### Simulation-side preview chain

- `src/game_loop_simulation_reuse_runtime_preview_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_presenter_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_bridge_presenter_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_bridge_presenter_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_presenter_ops.hpp`

### Scheduler/reuse aggregate

- `src/game_loop_scheduler_reuse_simulation_preview_contracts.hpp`
- `src/game_loop_scheduler_reuse_simulation_preview_assembler.hpp`
- `src/game_loop_scheduler_reuse_simulation_preview_presenter_ops.hpp`

### Presenter/resumo aggregate

- `src/game_loop_presenter_summary_scheduler_reuse_simulation_preview_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_preview_assembler.hpp`

### Presentation-facing narrowing

- `src/game_loop_presenter_summary_scheduler_reuse_simulation_view_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_view_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_presenter_ops.hpp`

## What this boundary proves

- the simulation-side reuse branch now reaches presenter/resumo level without
  touching runtime ownership
- the new simulation-side path no longer depends on the broad
  `FrameReuseRuntimeOwnerPacket` to be validated structurally
- a future runtime retry can target a much smaller local consumption point
  instead of recreating contracts first
- view/text narrowing is now available before any runtime reopening
- the presentation-facing consumer can stay compile-only while the live retry
  remains blocked

## Runtime status

- compile-only only
- no new runtime members
- no new live commit points
- no new live owner-packet joins
- no new substitution in `src/game_loop_system.hpp`

## Why this boundary is considered consolidated

It now has:

- explicit simulation-side preview chain
- explicit scheduler/reuse aggregate above that chain
- explicit presenter/resumo aggregate above that scheduler/reuse layer
- explicit view narrowing
- explicit text narrowing
- explicit presentation-facing helper
- host/runtime ownership preserved

## Safe next step

If this boundary stays stable, the next safe move is not a broad runtime retry.
The next safe move is one of:

1. identify one remove-first local presenter/debug line that this exact text
   path can replace
2. keep this boundary compile-only and document it as the future attach point
   for the next simulation-side live retry
3. continue documentation-first consolidation on an adjacent presenter/scheduler
   boundary before touching runtime

## Guard rails

- keep the ISO at `4134912`
- do not widen runtime state in `src/game_loop_system.hpp`
- do not reopen the combined simulation+track owner-packet seam first
- do not mix this path with scheduler timing or drain behavior changes
- prefer compile-only validation first

## Related documents

- `SIMULATION_REUSE_RUNTIME_DEBUG_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SCHEDULER_REUSE_SIMULATION_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SIMULATION_REUSE_SEAM_COMPLETION_PLAN.md`
- `SIMULATION_REUSE_LIVE_RETRY_BLOCKER.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `PRESENTER_SCHEDULER_REUSE_BOUNDARY_CONSOLIDATED.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
