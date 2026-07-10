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
- HUD/debug producer-state presentation fed from the same narrow telemetry view

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

## Accepted narrow live share

One additional narrow live share is now accepted inside the same presentation
path:

1. assemble `TrackRenderTelemetryViewPacket` once in the local frame-presentation path
2. reuse it for:
   - `Sh2SplitTelemetrySnapshot`
   - derived `TrackRenderProducerStatePacket`
3. keep all consumption local to the same call path

This does not authorize:

- producer ownership changes
- track scheduling changes
- persistent telemetry caching
- packet reuse across frames

One further narrow local retry remains prepared, but should stay disabled until
the current emulator-stable baseline is reconfirmed:

- assemble `TrackRenderPresentationObservabilityPacket`
- consume it only inside the same HUD/debug presentation helper
- remove the equivalent local producer-state formatting reads in the same patch
- prefer consuming `src/game_loop_track_render_presentation_observability_presenter_ops.hpp`
  rather than open-coding the producer-state line again

The currently accepted live form of that presenter helper is narrower:

- `PresentTrackRenderProducerStatePacket(...)` only
- `PresentTrackRenderSh2BusyLine(...)`
- `PresentTrackRenderSh2SimSafeLine(...)`
- `PresentTrackRenderSh2SimFallbackLine(...)`

The higher `TrackRenderPresentationObservabilityPacket` path remains staged but
disabled.

The next compile-only staging packet for that same boundary is now:

- `TrackRenderSh2PresentationPacket`

This packet remains compile-only until the emulator-stable baseline is
reconfirmed for another remove-first retry.

It groups only:

- `Sh2SplitTelemetrySnapshot`
- `TrackRenderProducerStatePacket`
- formatting mode flag for safe/fallback line selection
- dispatch counters already read locally by the host

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

That staging is now one level broader on the track side too:

- `TrackReuseDecisionViewPacket`
- `TrackReuseTelemetryViewPacket`
- `TrackReuseObservabilityPacket`
- `TrackReusePreviewPacket`

The first live retry anchored at that lower track-only seam is now documented
and accepted in:

- `TRACK_REUSE_OBSERVABILITY_FIRST_LIVE_RETRY_PLAN.md`

The compile-only bridge aligned to the same host seam remains:

- `BuildTrackReuseRuntimeObservabilityPacket(...)`
- `BuildTrackReuseRuntimePreviewPacket(...)`
- `PresentTrackReuseRuntimePreviewPacket(...)`

The exact accepted runtime patch at that seam is now recorded in:

- `TRACK_REUSE_OBSERVABILITY_FIRST_LIVE_RETRY_PATCH_PLAN.md`

The same rule also applies to accumulated reuse counters:

- derive a narrow `TrackReuseTelemetryViewPacket`
- keep it compile-only first
- do not wire it into a live pacing/scheduling path in the same step

For symmetry, the same compile-only preparation should exist on the simulation side:

- `SimulationReuseDecisionViewPacket`
- `SimulationReuseTelemetryViewPacket`

This keeps any later live reuse retry comparable across Master/Slave simulation
and track producer paths.

Before any broader live presentation retry on the track side, the preferred
compile-only staging packet is now:

- `TrackRenderPresentationObservabilityPacket`

It groups only:

- `TrackRenderTelemetryViewPacket`
- `TrackRenderProducerStatePacket`
- `Sh2SplitTelemetrySnapshot`

and intentionally excludes:

- `TrackRenderFramePacket`
- producer/sort scheduling ownership
- any cross-frame cache or reuse state

Before any broader live track-reuse retry, prefer completing the remaining
symmetric simulation-side branch before jumping from the accepted track-only
seam into the broader cross-domain `ReuseObservabilityPacket`.

Keep `TrackReusePreviewPacket` compile-only as the guard rail immediately above
that staging path.
