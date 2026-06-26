# TrackRender Minimal Live Substitution Plan

## Objective

Define the smallest acceptable future live substitution order for track-side
telemetry sharing, using only `TrackRenderTelemetryViewPacket`.

This plan exists because both `src/game_loop_system.hpp` and the track pipeline
are sensitive to small runtime layout changes.

## Runtime boundaries covered

Only these runtime touch points are in scope:

- `IsTrackProducerJobInFlightHint(...)`
- overlay query diagnostics fed from track telemetry
- SH2 query/busy presentation fed from track telemetry

No producer/sort ownership move is allowed in the same patch series.

## Narrow packet to use

Use only:

- `TrackRenderTelemetryViewPacket`

Current decision/data surface:

- `producerJobInFlight`
- `producerSafeModeActive`
- `masterFrameTicks`
- `slaveProducerTicks`
- `slaveSortTicks`
- `slavePlanTicks`
- `queryCalls`
- `queryGlobal`
- `queryScmap`
- `queryCacheHits`
- `queryCacheMisses`
- `wallQueryCalls`
- `wallQueryHits`

## Required substitution order

### Step 1 - overlay and SH2 consumers first

The first live retry, if any, must target only the repeated telemetry
consumers, not scheduling.

Patch shape:

1. assemble `TrackRenderTelemetryViewPacket` locally
2. consume it only in:
   - overlay query population
   - SH2 query/busy presentation
3. replace only equivalent local telemetry reads
4. keep packet assembly local to the same scope

Must remain unchanged:

- `TrackSystem` ownership
- producer/sort behavior
- fallback behavior
- safe mode behavior
- render submission order

### Step 2 - producer in-flight hint second

Only after repeated stable runs from Step 1:

1. reuse the same narrow packet for `IsTrackProducerJobInFlightHint(...)`
2. replace only equivalent in-flight reads
3. keep the hint behavior identical

Must remain unchanged:

- producer scheduling
- Master/Slave orchestration
- frame pacing

## What must not be pulled into the live boundary

Do not reintroduce these directly into the runtime call site first:

- `TrackRenderFramePacket`
- `TrackRenderDebugPacket`
- `TrackFrameContext`
- `TrackRenderPacket`
- direct scheduler extraction

Those structures may remain upstream/off-path, but the live boundary should
consume only the narrow chain:

- `TrackRenderTelemetryViewPacket`
- `TrackRenderProducerStatePacket`
- `TrackRenderProducerHintPacket`

## Remove-first rule

Each live patch must be substitutional.

That means:

- if a packet field replaces a local telemetry fetch, the original fetch must be
  removed in the same patch
- if no equivalent read is removed, the packet must stay compile-only

## Acceptance criteria

Every live patch in this sequence must keep:

- ISO exactly `4134912`
- emulator startup stable
- no invalid opcode
- no silent close
- no visual pacing drift
- no track telemetry drift

## Validation ritual

Required after every live attempt:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Abort conditions

Rollback immediately if:

- ISO grows above `4134912`
- emulator no longer boots
- invalid opcode appears
- the boundary needs more than `TrackRenderTelemetryViewPacket`
- scheduling behavior starts to move together with telemetry substitution

## Follow-up passive target

After the producer hint chain is stable, the next safe work item is not another
live scheduling move.

It is compile-only preparation of a narrow `TrackReuse` decision view packet
derived from `TrackReuseDecisionPacket`, so a future retry can consume reuse
decisions without taking the full history/decision structure into the live
call site first.

The same rule also applies to accumulated reuse counters:

- derive a narrow `TrackReuseTelemetryViewPacket`
- keep it compile-only first
- do not wire it into a live pacing/scheduling path in the same step

For symmetry, the same compile-only preparation should exist on the simulation side:

- `SimulationReuseDecisionViewPacket`
- `SimulationReuseTelemetryViewPacket`

This keeps any later live reuse retry comparable across Master/Slave simulation
and track producer paths.
