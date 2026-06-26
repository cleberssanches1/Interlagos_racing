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

The `scheduler/reuse observability` chain is currently compile-only.

No scheduler/reuse passive packet is consumed live in critical runtime files:

- `SimulationDrainViewPacket`
- `SimulationCompletionViewPacket`
- `SimulationSchedulerTelemetryViewPacket`
- `TrackRenderProducerStatePacket`
- `ReuseObservabilityPacket`
- `SimulationSchedulerLifecycleObservabilityPacket`
- `SchedulerReuseObservabilityPacket`
- `SchedulerReuseFlowObservabilityPacket`

Current runtime ownership remains local at the existing scheduler, simulation,
and producer call sites.

## Current prepared passive boundaries

### Boundary A - local scheduler telemetry assembly

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

### Boundary B - local producer-state assembly

Prepared packet:

- `TrackRenderProducerStatePacket`

Intended consumer shape:

- the same local observability-facing path as Boundary A
- or one immediate sibling helper

Equivalent local reads expected to be replaced:

- `producerJobInFlight`
- `producerSafeModeActive`

### Boundary C - reuse family aggregate

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

### Boundary D - scheduler lifecycle aggregate

Prepared packet:

- `SimulationSchedulerLifecycleObservabilityPacket`

Contents:

- `SimulationDrainViewPacket`
- `SimulationCompletionViewPacket`
- `SimulationSchedulerTelemetryViewPacket`

Intended consumer shape:

- one scheduler-only observability helper
- no dispatch/drain behavior changes

### Boundary E - scheduler/reuse aggregate

Prepared packets:

- `SchedulerReuseObservabilityPacket`
- `SchedulerReuseFlowObservabilityPacket`

Intended consumer shape:

- one observability-facing or presenter-facing read boundary
- only after lower layers are already proven stable live

## Recommended first live boundary

The first acceptable future live retry should target only Boundary A.

Patch shape:

1. assemble `SimulationSchedulerTelemetryViewPacket` locally
2. consume it in exactly one debug/telemetry-facing helper
3. remove equivalent local scheduler counter reads in the same patch
4. keep packet assembly stack-local

Why this is first:

- it is narrower than reuse aggregates
- it does not require producer ownership movement
- it avoids touching `N-1` reuse semantics
- it fits the remove-first rule cleanly

## Current prohibited live moves

Do not do these in the first scheduler/reuse live retry:

- consume `SchedulerReuseObservabilityPacket` first
- consume `SchedulerReuseFlowObservabilityPacket` first
- mix scheduler telemetry substitution with producer scheduling changes
- mix observability substitution with drain policy changes
- mix reuse packetization with `N-1` policy behavior changes
- move `TrackSystem` producer control into a new owner

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
