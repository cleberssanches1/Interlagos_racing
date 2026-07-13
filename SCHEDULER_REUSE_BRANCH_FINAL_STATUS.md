# Scheduler / Reuse Branch Final Status

## Objective

Resolve the current ambiguity around `scheduler/reuse` in this branch by
stating explicitly:

- what live seam is accepted as final
- what broader retry path is formally frozen

This document is branch-closure inventory.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- scheduler/producer ownership remains in:
  - `src/game_loop_system.hpp`
  - `src/track_system.hpp`

## Final branch status

Status for this branch:

- narrow active seam accepted as final
- broader symmetric simulation-side expansion formally frozen

## Accepted final live seam

The accepted final live `scheduler/reuse` shape for this branch is:

1. local `SimulationSchedulerTelemetryViewPacket` assembly
2. local `TrackRenderProducerStatePacket` consumption
3. one track-only reuse observability seam through:
   - local `TrackReuseRuntimeState`
   - real `FrameReuseRuntimeOwnerPacket`
   - `TryBuildReuseObservabilityDebugBundle(...)`

This is the live seam that should now be treated as the final accepted runtime
result for the current branch.

## What is formally frozen

The following broader expansion is now formally frozen for this branch:

- symmetric simulation-side live reuse completion
- broader owner-packet join above the current track-only seam
- larger live scheduler/reuse aggregate retry

Reason:

- code-size pressure at the fixed ISO envelope
- emulator instability when the seam is widened too early
- no recovered always-live budget currently justifies reopening the branch

## Freeze condition

Do not reopen the broader `scheduler/reuse` live branch unless one of these is
true:

1. at least `2048` bytes of always-live budget are recovered for the seam join
   retry
2. at least `4096` bytes of always-live budget are recovered for the broader
   Boundary D retry path
3. the fixed-ISO strategy is explicitly changed

Absent one of those conditions, the broader branch remains frozen.

## What this resolves

This resolves the prior ambiguity between:

- “partially active”
- and “blocked but still pending”

The correct reading after this document is:

- the narrow seam is final for this branch
- the broader branch is not pending immediate completion
- it is deferred beyond this branch unless the preconditions above change

## What still stays outside

The following concerns remain intentionally outside the accepted final seam:

- broader symmetric simulation-side reuse ownership
- wider live scheduler/reuse observability aggregate
- larger presenter/resumo growth above the accepted seam

## Recommended interpretation in closure docs

From this point onward:

- do not list `scheduler/reuse` as unresolved remaining work for this branch
- do list it as:
  - actively consolidated at the narrow seam
  - broader branch frozen with rationale
