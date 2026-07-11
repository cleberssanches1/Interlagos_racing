# Refactor Closure Execution Plan

## Objective

Define the execution plan required to finish the current refactor without
reopening broad exploratory loops.

This document is not another architecture inventory.

It is the execution contract for closing the remaining runtime-facing work.

## Closure rule

The refactor is considered finished when every remaining subsystem is in one of
these terminal states:

- accepted live cut
- compile-only frozen boundary
- blocked-final with explicit reason

No subsystem should remain in an open-ended "maybe later" state without one of
those outcomes.

## Global invariants

Every runtime attempt below must preserve:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- one boundary only per patch
- remove-first substitution in the same patch

Validation after any runtime attempt:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Anti-circle rules

These rules are mandatory.

1. one active subsystem at a time
2. one runtime boundary at a time
3. one runtime retry at a time
4. one explicit pass/fail decision after each retry
5. no broader boundary may open before the narrower sibling is either accepted
   or frozen
6. if the same structural failure repeats twice, the boundary leaves the active
   plan and becomes frozen or blocked-final

Structural failures include:

- ISO growth above `4134912`
- emulator boot regression
- invalid opcode
- required widening of the boundary beyond the prepared narrow packet

## Execution phases

### Phase 1 - presenter decision

Target:

- validate the already-live HUD status-line cut and decide whether Presenter is
  accepted or frozen

Action:

1. keep the current HUD status-line live cut unchanged
2. user tests emulator stability and visible behavior
3. if needed, run the full validation gate after any follow-up adjustment
4. do not widen presenter runtime scope in the same step

Decision:

- if stable, mark Presenter as `accepted live cut`
- if broader presenter work remains unjustified, freeze the remaining
  presenter runtime boundaries as compile-only
- if the current live cut itself regresses, revert it and freeze Presenter as
  compile-only

Terminal document:

- update `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`

### Phase 2 - track render decision

Target:

- validate the already-live local SH2 presentation cut and decide whether Track
  Render is accepted or frozen

Action:

1. keep the current local SH2 presentation live cut unchanged
2. user tests emulator stability and SH2/HUD debug output
3. if needed, run the full validation gate after any follow-up adjustment
4. do not widen Track Render runtime scope in the same step

Decision:

- if stable, mark Track Render as `accepted live cut`
- if broader Track Render work remains unjustified, freeze the remaining higher
  Track Render runtime boundaries as compile-only
- if the current live cut itself regresses, revert it and freeze Track Render
  at the lower accepted live cuts

Terminal document:

- update `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`

### Phase 3 - scheduler/reuse decision

Target:

- keep only the already accepted partial simulation-side live progress unless a
  separate size-reduction pass reopens the seam join safely

Action:

1. keep the simulation-history commit points live
2. keep the track-only seam live
3. do not reopen the combined owner-packet join in the same closure pass
4. run the full validation gate after any follow-up adjustment
5. user tests emulator stability and observability output

Decision:

- if stable, mark Scheduler/Reuse as `partial live cut accepted`
- mark the combined owner-packet seam join as `blocked-final for this closure
  pass` unless a separate size-reduction pass is explicitly started

Terminal document:

- update `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`

### Phase 4 - car render decision

Target:

- shadow-prep remove+replace only

Action:

1. remove duplicated local shadow-prep reads
2. keep draw entrypoints and submit path unchanged
3. run the full validation gate
4. user tests emulator boot and visible shadow behavior

Decision:

- if stable, mark Car Render as `accepted live cut`
- if unstable twice for the same structural reason, freeze shadow-prep as
  compile-only and do not reopen broad `CarVisualFramePacket` work

Terminal document:

- update `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`

### Phase 5 - memory budget decision

Target:

- one extra category-local bridge consumer only

Action:

1. choose one local gate or pool-choice path
2. replace only that exact local logic
3. keep allocator timing unchanged
4. run the full validation gate
5. user tests emulator stability and memory/debug behavior

Decision:

- if stable and useful, mark Memory Budget as `accepted extra live cut`
- if low-value or unstable twice, freeze further runtime widening and keep the
  current bridge-level live cuts as final

Terminal document:

- update `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`

## Deferred or frozen by default

These are not active closure targets now:

- broad presenter aggregate live retry
- broad scheduler/reuse aggregate live retry
- full `CarVisualFramePacket` runtime handoff
- broad `MemoryBudgetFramePacket` live integration
- broader CD/bootstrap widening
- AutoLap live runtime moves

They should remain frozen unless all closure phases above are completed and a
new explicit decision reopens them.

## Test gates

After each phase, stop and test before continuing.

Required gate per phase:

1. build/validator gate passes
2. user confirms emulator boots
3. user confirms no regression in the touched boundary
4. only then move to the next phase

This is mandatory.

## Completion checklist

The refactor is closed only when all items below are true:

- Presenter is accepted or frozen
- Track Render is accepted or frozen
- Scheduler/Reuse is accepted as partial live progress or blocked-final
- Car Render is accepted or frozen
- Memory Budget is accepted or frozen
- CD/bootstrap is explicitly kept frozen at the current narrow accepted state
- AutoLap is explicitly kept passive/frozen
- `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md` reflects the final state of each
  subsystem
- one final closure summary document records:
  - accepted cuts
  - frozen boundaries
  - blocked-final boundaries
  - safe maintenance order

## Immediate next step

The next execution step is:

1. start with Phase 1 only
2. user tests the current presenter HUD status-line live cut
3. user tests the current Track Render SH2 presentation live cut
4. only after that move to Phase 3

## Related documents

- `REFACTOR_CONSOLIDATED_FINAL_INDEX.md`
- `REFACTOR_RUNTIME_SAFE_NEXT_CUTS.md`
- `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`
- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`
- `MEMORY_BUDGET_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
