# Track Reuse Observability First Live Retry Patch Plan

## Objective

Record the exact accepted runtime patch for the track-only reuse branch at the
real host seam, without widening to the broader cross-domain symmetric reuse
aggregate.

## Exact host seam

Target only this local path in `src/game_loop_system.hpp`:

- `TryBuildReuseObservabilityDebugBundle(...)`

Current local behavior:

- it calls `TryBuildReuseObservabilityDebugBundle(...)`
- it now feeds `GameLoopRuntime::BuildTrackReuseRuntimeOwnerPacket(trackReuseState_)`
- runtime behavior therefore no longer stays neutral on the track side

This remained the correct retry anchor because the accepted live retry replaced
the empty owner capture at the same seam.

## Exact accepted retry shape

The accepted runtime patch now:

1. keep the method local in `src/game_loop_system.hpp`
2. stop using `CaptureEmptyFrameReuseRuntimeOwnerPacket()`
3. capture only track-side runtime owner data
4. commit only track history and current request snapshot locally
5. build one track-only runtime owner packet on demand
6. keep simulation-side reuse neutral in the same retry
7. keep the consumer localized to the same reuse debug/presenter seam

The new narrow runtime support now lives in:

- `src/game_loop_track_reuse_runtime_state_contracts.hpp`
- `src/game_loop_track_reuse_runtime_state_ops.hpp`

## Prepared compile-only path

The compile-only helpers already aligned to this patch are:

- `src/game_loop_track_reuse_runtime_bridge_assembler.hpp`
- `src/game_loop_track_reuse_runtime_preview_presenter_ops.hpp`
- `src/game_loop_track_reuse_preview_presenter_ops.hpp`

Current compile-only chain:

1. `FrameReuseRuntimeOwnerPacket`
2. `BuildTrackReuseRuntimeObservabilityPacket(...)`
3. `BuildTrackReuseRuntimePreviewPacket(...)`
4. `PresentTrackReuseRuntimePreviewPacket(...)`

## Remove-first interpretation at this seam

This retry was substitutional because it replaced:

- the current empty runtime-owner capture

with:

- a real narrow track-only runtime-owner capture at the same call site

without adding a second parallel reuse presentation path.

## What must stay unchanged

- simulation-side reuse capture
- scheduler dispatch behavior
- track producer behavior
- fallback behavior
- final presentation ordering outside the reuse seam

These remained unchanged in the accepted patch.

## What the accepted retry intentionally did not do

It did not do these in the same patch:

- wire simulation-side reuse live together with track-side reuse
- consume `SchedulerReuseObservabilityPacket`
- move ownership out of `src/game_loop_system.hpp`
- change track producer or scheduler policy

## Accepted patch sketch

Conceptually, the accepted runtime retry reduced to:

1. add one local `TrackReuseRuntimeState`
2. capture request-side track reuse inputs in `RenderTrackFrame(...)`
3. commit track-frame history in the same local runtime flow
4. build one track-only `FrameReuseRuntimeOwnerPacket` on demand
5. feed that packet into the same local `outBundle` seam
6. remove the previous empty capture from that same method

## Accepted validation result

- ISO remains `4134912`
- stable build passed
- passive headers passed
- observability headers passed
- no scheduler/producer behavior drift was introduced
- reuse debug output activates only for the intended track-side branch

## Envelope note

The code-side retry still carried the previously measured `4096`-byte live
budget cost.

The stable ISO envelope was preserved by shrinking the inert
`cd/data/ISO_PAD_4K.BIN` pad while keeping the final ISO fixed at `4134912`.

## Validation ritual used

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`
- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

## Next safe patch above this one

The next safe retry should not widen ownership.

It should either:

1. keep broad reuse aggregation compile-only and document the new low live
   boundary, or
2. attempt the next remove-first symmetric simulation-side reuse activation in
   the same seam only after the replacement reads are clearly identified
