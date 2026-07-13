# Simulation Reuse Runtime Debug Preview Boundary Consolidated

## Objective

Consolidate the current compile-only local debug boundary that narrows the
simulation-side reuse branch before it joins any broader scheduler/reuse
aggregate.

This document is inventory-only.

It does not authorize runtime ownership changes by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the critical runtime host
- the accepted live seam remains the track-only reuse activation
- simulation-side reuse remains compile-only at this boundary

## Boundary hierarchy

Exact local chain:

1. `SimulationReuseRuntimeDecisionInputsPacket`
2. `SimulationReuseDecisionViewPacket`
3. `SimulationReuseRuntimePreviewPacket`
4. `SimulationReuseRuntimeDebugPreviewPacket`
5. `PresentSimulationReuseRuntimePreviewPacket(...)`
6. `PresentSimulationReuseRuntimeDebugPreviewPacket(...)`

This keeps the simulation-side branch observable without requiring a live
`FrameReuseRuntimeOwnerPacket` expansion.

## Files

### Decision and preview inputs

- `src/game_loop_simulation_reuse_runtime_decision_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_decision_assembler.hpp`
- `src/game_loop_simulation_reuse_decision_view_contracts.hpp`
- `src/game_loop_simulation_reuse_decision_view_assembler.hpp`
- the former runtime preview pair was removed during later cleanup after smoke
  validation stopped depending on it

### Local presentation-facing debug boundary

- the former local debug preview ladder above that pair was also removed during
  later cleanup after smoke validation stopped depending on it

## What this boundary isolates

- exact host-local simulation reuse decision inputs
- derived simulation reuse decision preview, now represented directly by the
  remaining decision/view/bridge layers without a standalone preview ladder
- one explicit local debug-preview packet above that preview
- one explicit local presenter helper above that debug-preview packet

This lets future work reopen the simulation-side branch at a smaller local
debug boundary instead of restarting from the combined runtime-owner seam.

## Runtime status

- compile-only only
- no new runtime members
- no live owner-packet expansion
- no scheduler timing changes
- no track-side ownership changes

## Why this boundary matters

- it is the narrowest explicit simulation-side reuse presentation boundary now
  available in source
- it reduces pressure to retry the broader symmetric seam first
- it provides a direct local attach point for future remove-first debug lines
- it keeps simulation-side reasoning separate from scheduler/reuse aggregates

## Safe next step

The next safe move is still documentation-first or preview-first:

1. keep this boundary compile-only
2. consume it only through higher compile-only aggregates
3. if runtime is retried later, replace one exact local debug line in the same
   patch

## Guard rails

- keep ISO at `4134912`
- do not widen `FrameReuseRuntimeOwnerPacket` first
- do not mix this boundary with scheduler drain or producer scheduling changes
- do not move simulation history ownership out of the host in the same patch

## Related documents

- `SIMULATION_REUSE_SEAM_COMPLETION_PLAN.md`
- `SIMULATION_REUSE_LIVE_RETRY_BLOCKER.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `SCHEDULER_REUSE_SIMULATION_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `PRESENTER_SUMMARY_SCHEDULER_REUSE_SIMULATION_BOUNDARY_CONSOLIDATED.md`
