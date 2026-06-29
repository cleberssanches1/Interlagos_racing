# Scheduler / Reuse Live Integration Inventory

## Objective

Document the exact live integration status of the `scheduler/reuse
observability` chain and define the narrowest acceptable first live boundary.

This document is runtime-facing inventory only.

It does not authorize a live patch by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- no invalid opcode
- no silent close
- scheduler/producer ownership must remain in:
  - `src/game_loop_system.hpp`
  - `src/track_system.hpp`

## Current live status

The `scheduler/reuse observability` chain is now partially live through two
narrow local observability-only substitutions.

No broad scheduler/reuse aggregate is consumed live in critical runtime files:

- `SimulationDrainViewPacket`
- `SimulationCompletionViewPacket`
- `ReuseObservabilityPacket`
- `SimulationSchedulerLifecycleObservabilityPacket`
- `SchedulerReuseObservabilityPacket`
- `SchedulerReuseFlowObservabilityPacket`

Current runtime ownership remains local at the existing scheduler, simulation,
and producer call sites.

## Current live retry blocker

The first live retry attempt for `Boundary D` was not retained.

Reason:

- it pushed the ISO from `4134912` to `4139008`
- exact regression budget was `4096` bytes

Current required pre-step:

- reduce debug / telemetry code size first using
  `DEBUG_TELEMETRY_SIZE_REDUCTION_PLAN.md`
- latest reattempt result confirms the blocker still holds:
  - decision-first retry still raised the ISO to `4139008`
  - cumulative telemetry was not the cause

## Current live boundary

### Boundary A - local scheduler telemetry assembly

Live file:

- `src/game_loop_system.hpp`

Live accessor:

- `BuildSh2SplitTelemetrySnapshot()`

Current live timing:

- assembled during frame presentation snapshot generation

Current ownership kept local:

- scheduler runtime ownership
- dispatch/drain behavior
- track producer ownership

Current live substitution:

- `SimulationSchedulerTelemetryViewPacket` is now assembled locally from
  `simState_`
- equivalent local broad `SimulationSchedulerTelemetry` assembly was removed

## Current prepared passive boundaries

### Boundary B - local scheduler telemetry assembly follow-up

Prepared packet:

- `SimulationSchedulerTelemetryViewPacket`

Intended first consumer shape:

- one stack-local debug/telemetry assembly point
- one presenter/debug-only read path

Equivalent local reads expected to be replaced first:

- dispatch counters
- track-busy skips
- backoff skips
- drain timeout / hard-wait counters
- master/slave timing counters

### Boundary C - local producer-state assembly

Live file:

- `src/game_loop_system.hpp`

Live accessor:

- `PresentFrameHudAndTelemetry(...)`
- `PrintSh2SplitTelemetry(...)`

Current live timing:

- assembled and consumed during frame presentation/debug telemetry

Current ownership kept local:

- track producer ownership
- SH2 presentation/debug ownership
- scheduler/reuse observability ownership

Current live substitution shape:

- `TrackRenderProducerStatePacket` is assembled locally through
  `TryBuildTrackRenderProducerStatePacket(...)`
- the packet is consumed in a local presentation/debug-only helper
- `Sh2SplitTelemetrySnapshot` layout remains unchanged

Prepared packet:

- `TrackRenderProducerStatePacket`

Intended consumer shape:

- the same local observability-facing path as Boundary A
- or one immediate sibling helper

Equivalent local reads expected to be replaced:

- `producerJobInFlight`
- `producerSafeModeActive`

### Boundary D - reuse family aggregate

Prepared packet:

- `ReuseObservabilityPacket`

Contents:

- `SimulationReuseDecisionViewPacket`
- `SimulationReuseTelemetryViewPacket`
- `TrackReuseDecisionViewPacket`
- `TrackReuseTelemetryViewPacket`

Intended consumer shape:

- one local debug/presenter-only assembly helper
- no ownership transfer

Prepared off-path chain helper:

- `src/game_loop_scheduler_reuse_observability_assembly_ops.hpp`

Prepared derived debug-view helper:

- `src/game_loop_reuse_observability_debug_contracts.hpp`
- `src/game_loop_reuse_observability_debug_assembler.hpp`

Prepared derived presenter helper:

- `src/game_loop_reuse_observability_debug_presenter_ops.hpp`

Prepared derived local bundle:

- `src/game_loop_reuse_observability_debug_bundle_contracts.hpp`
- `src/game_loop_reuse_observability_debug_bundle_assembler.hpp`

Prepared derived local bundle presenter:

- `src/game_loop_reuse_observability_debug_bundle_presenter_ops.hpp`

Current live posture:

- compile-only groundwork remains ready
- runtime retry is deferred until the `4 KB` budget is recovered
- first size-reduction pass already removed duplicate packet-overload glue from
  the bundle/local helper stack
- second size-reduction pass already compressed the reuse presenter from two
  lines into one compact line
- third size-reduction pass already removed the extra compile-only local helper
  layer and left the bundle presenter as the narrowest remaining helper
- even after those reductions, the decision-first live retry still hit the same
  `+4096` byte ISO regression and was reverted

### Boundary E - scheduler lifecycle aggregate

Prepared packet:

- `SimulationSchedulerLifecycleObservabilityPacket`

Contents:

- `SimulationDrainViewPacket`
- `SimulationCompletionViewPacket`
- `SimulationSchedulerTelemetryViewPacket`

Intended consumer shape:

- one scheduler-only observability helper
- no dispatch/drain behavior changes

### Boundary F - scheduler/reuse aggregate

Prepared packets:

- `SchedulerReuseObservabilityPacket`
- `SchedulerReuseFlowObservabilityPacket`

Intended consumer shape:

- one observability-facing or presenter-facing read boundary
- only after lower layers are already proven stable live

Prepared off-path chain helper:

- `src/game_loop_scheduler_reuse_observability_assembly_ops.hpp`

## Recommended first live boundary

The first acceptable future live retry after the current live cuts should target
only Boundary D.

Patch shape:

1. reuse the already-live local scheduler/producer observability helpers
2. add `ReuseObservabilityPacket` in the same local observability-facing helper
   or its immediate sibling
3. remove equivalent reuse-view reads in the same patch
4. keep packet assembly stack-local

Why this is next:

- Boundary A is already active and stable
- Boundary C is already active and stable
- reuse observability is the next already-prepared aggregate above those cuts
- it still avoids touching `N-1` reuse semantics
- it keeps the retry substitutional

## Current prohibited live moves

Do not do these in the first scheduler/reuse live retry:

- consume `SchedulerReuseObservabilityPacket` first
- consume `SchedulerReuseFlowObservabilityPacket` first
- mix scheduler telemetry substitution with producer scheduling changes
- mix observability substitution with drain policy changes
- mix reuse packetization with `N-1` policy behavior changes
- move `TrackSystem` producer control into a new owner

## Current investigation posture

Because recent ultra-narrow retries could still regress emulator startup even
while preserving ISO `4134912`, the immediate next step should prefer external
instrumentation over another live retry in `src/game_loop_system.hpp`.

Use:

- `EMULATOR_DIVERGENCE_EXTERNAL_INSTRUMENTATION_PLAN.md`
- `kronos_ai_trace_plan.md`
- `kronos_trace_bridge_spec.md`
- `kronos_trace_hook_map.md`
- `kronos_watch_quick_test.md`

## Remove-first rule

Any future scheduler/reuse live patch must:

1. target one boundary only
2. consume the narrowest already-prepared packet for that boundary
3. remove equivalent local reads in the same patch
4. keep dispatch/drain/producer behavior unchanged

## Acceptance criteria for a future live retry

Every future scheduler/reuse live patch must keep:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- no frame pacing drift
- no lockstep/drain regressions
- no producer-safe-mode regressions

## Validation ritual

Required after every future live attempt:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Related documents

- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`
- `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PASSIVE_TO_RUNTIME_INTEGRATION_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
- `EMULATOR_DIVERGENCE_EXTERNAL_INSTRUMENTATION_PLAN.md`
