# Track Reuse Observability First Live Retry Plan

## Objective

Record the first accepted live retry on the track-reuse branch, anchored at the
existing reuse observability seam, without widening immediately to the broader
cross-domain symmetric reuse path.

## Exact runtime anchor

The correct local host seam is the existing reuse observability path in
`src/game_loop_system.hpp`:

- `TryBuildReuseObservabilityDebugBundle(...)`
- `PresentFrameHudAndTelemetry(...)`

Current local behavior:

- the host calls `TryBuildReuseObservabilityDebugBundle(reuseBundle)`
- that path now feeds
  `GameLoopRuntime::BuildTrackReuseRuntimeOwnerPacket(trackReuseState_)`
- runtime behavior is therefore no longer neutral on the track side

## Why this is the right anchor

- it is already observability-only
- it already feeds a debug presenter path
- it does not change dispatch, producer ownership, or render submission
- it is the narrowest real host seam that can accept real track reuse data
  without widening ownership

## Accepted live shape

The accepted live retry now does only this:

1. capture one narrow local track-side runtime snapshot in
   `TrackReuseRuntimeState`
2. commit only track history locally during `RenderTrackFrame(...)`
3. build one track-only `FrameReuseRuntimeOwnerPacket` on demand at the
   existing reuse seam
4. derive `TrackReuseDecisionPacket` on demand from the committed history and
   current request snapshot
5. keep simulation-side reuse empty in the same seam
6. keep cumulative reuse telemetry out of the live runtime path

The narrow live runtime state is now:

- `history`
- `requestFrameId`
- `activeSegmentId`
- `renderEnabled`
- `producerJobInFlight`

It intentionally does not keep:

- simulation reuse state
- cumulative reuse telemetry
- broader scheduler/reuse aggregates

## Required first live boundary

The first accepted live retry consumes only the track-side branch required to
activate the existing local reuse debug seam:

1. `TrackReuseDecisionViewPacket`
2. the already existing local reuse debug/bundle path above it

It still does not widen to:

- symmetric live population of `ReuseObservabilityPacket`
- `SchedulerReuseObservabilityPacket`
- `SchedulerReuseFlowObservabilityPacket`

## Applied patch shape

Inside the local reuse observability path:

1. replace the empty runtime-owner capture
2. capture real runtime inputs for the track side only
3. build one real track-only `FrameReuseRuntimeOwnerPacket`
4. feed that packet into the already existing local reuse debug seam
5. keep simulation-side reuse neutral in that same retry
6. keep telemetry counters passive/off-path

## Remove-first rule

This retry remained valid because the seam substitution was exact:

- the empty runtime-owner capture was removed from the same host seam
- the real track-only runtime-owner capture replaced it in place

## What must remain unchanged

- `TrackSystem` producer behavior
- lockstep wait policy
- synchronous fallback behavior
- scheduler dispatch policy
- final SH2 presentation ordering outside the local reuse debug seam

These still remain unchanged after the accepted retry.

## Good first consumer shape

The accepted first live consumer remains:

- one local debug/presenter helper
- track-only
- stack-local
- observability-only

The preferred local consumer is a narrow helper above:

- `TrackReuseObservabilityPacket`

not a broader scheduler aggregate.

## Remaining compile-only guard rails

The passive chain still prepared above or beside that retry is:

1. `TrackReuseDecisionViewPacket`
2. `TrackReuseTelemetryViewPacket`
3. `TrackReuseObservabilityPacket`
4. `TrackReusePreviewPacket`

That means the accepted live retry stays below the preview, and the preview
remains only a compile-time/documentation guard rail.

## Accepted validation result

- stable build passed with ISO `4134912`
- emulator-stable baseline was preserved
- passive headers passed
- observability headers passed
- no producer/scheduler ownership drift was introduced
- no simulation-side reuse coupling was introduced

## Validation ritual used

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`
- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

## Envelope note

The runtime code-side retry still consumed the known `4096`-byte budget.

The stable ISO envelope was preserved by reducing the inert
`cd/data/ISO_PAD_4K.BIN` pad accordingly, while keeping the required final ISO
size at `4134912`.

## Next safe step

Do next:

- keep this track-only seam stable
- document it as the new low live boundary on the reuse axis
- reopen the broader symmetric `ReuseObservabilityPacket` path only after a
  remove-first patch is obvious for the simulation-side branch

Do not do next:

- add simulation-side reuse live in the same patch
- add cumulative telemetry to this live path first
- jump directly to `SchedulerReuseObservabilityPacket`
