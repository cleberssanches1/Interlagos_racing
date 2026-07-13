# Bootstrap/CD and Scheduler/Reuse Action Plan

## Objective

Resolve what still truly remains in the two most sensitive remaining axes:

- `bootstrap/CD`
- `scheduler/reuse`

This document exists to stop circular work and define the correct next action
under the current fixed-ISO and emulator-stability rules.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- no invalid opcode
- no silent close

## Analysis result

## 1. Bootstrap/CD

Current status:

- actively consolidated at a narrow local seam
- bootstrap ownership still remains correctly in `src/main.cxx`
- broader `CdAssetFramePacket` runtime integration is already explicitly
  deferred

What is already solved:

- SBA bootstrap consumption is narrowed behind explicit helpers
- anchor fallback consumption is narrowed behind explicit helpers
- car visual bootstrap composition is narrowed behind explicit helpers
- staging preference is narrowed behind a bridge query

What is still theoretically possible later:

- broader request/read/parse packet consumption in `src/main.cxx`
- generalized `CdAssetSystem` bootstrap facade
- broader bootstrap ownership migration

What is actually missing for this branch:

- nothing mandatory

Correct reading:

- `bootstrap/CD` is closed for the current refactor branch
- any further move there is a new explicit runtime goal

## 2. Scheduler/Reuse

Current status:

- narrow live seam accepted as final
- broader simulation-side completion branch formally frozen

What is already solved:

- local scheduler telemetry view is live
- local producer-state consumption is live
- one track-only reuse seam is live
- the local owner/source/assembly ladder was already reduced to the smallest
  practical accepted shape

What still exists in theory:

- simulation-side seam completion
- broader symmetric owner join
- larger scheduler/reuse observability retry

What is actually missing for this branch:

- no mandatory runtime work

Current blocker is explicit:

- fixed-ISO envelope sensitivity
- prior retries already failed at `+2048` and `+4096`
- no recovered always-live budget currently justifies reopening the branch

Correct reading:

- `scheduler/reuse` is also closed for the current branch at the accepted seam
- broader work there is not pending; it is frozen behind explicit preconditions

## Combined decision

The correct action is not to keep coding inside these two subsystems now.

The correct action is:

1. treat `bootstrap/CD` as closed at the accepted live seam
2. treat `scheduler/reuse` as closed at the accepted narrow seam and frozen
   above it
3. stop listing either subsystem as unresolved mandatory branch work
4. only reopen either one if the user explicitly starts a new runtime goal

## Reopen conditions

## Bootstrap/CD may be reopened only if:

1. bootstrap ownership migration becomes an explicit goal, or
2. broader `CdAssetFramePacket` runtime use is explicitly authorized

## Scheduler/Reuse may be reopened only if:

1. at least `2048` bytes of always-live budget are recovered for the narrow
   simulation-side seam retry, or
2. at least `4096` bytes are recovered for the broader retry, or
3. the fixed-ISO strategy is explicitly changed

## Executed action for this branch

This plan executes as a branch-level closure decision:

- no new runtime patch is applied in either subsystem
- both subsystems are now treated as non-blocking for refactor closure
- future effort should move to another subsystem or a newly declared runtime
  goal

## Recommended next focus outside these two

Prefer one of these instead:

1. another subsystem with unresolved runtime value and lower envelope risk
2. explicit performance work with measurable profiler targets
3. explicit audio/gameplay/runtime goals chosen by the user
