# Refactor Final Remaining Work

## Objective

List only the remaining work required to consider the current refactor program
functionally closed, without repeating already consolidated areas.

This document is closure-oriented.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `AUDIO_PROFILE=1`
- no cross-subsystem runtime merge in one patch

## Remaining work only

### 1. Car Render active closure

Status:

- Remaining
- Structurally prepared, runtime-constrained

Why it remains:

- passive packetization is already adequate
- broader live `CarVisualFramePacket` integration exceeded the fixed ISO target
- no accepted active boundary yet replaced the host-owned runtime slice

What still needs to happen:

1. execute one binary-neutral remove-first runtime cut
2. keep that cut limited to the shadow-prep/shared-data slice first
3. prove one accepted active boundary in the live path, or formally leave the
   subsystem passive for this branch

Closure criterion:

- either one narrow active runtime boundary is accepted
- or the subsystem is explicitly frozen as passive-only for this branch

Primary docs:

- `CAR_RENDER_BOUNDARY_CLOSURE_CONSOLIDATED.md`
- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`

### 2. AutoLap active closure

Status:

- Remaining
- Structurally prepared, runtime-deferred

Why it remains:

- route/build/lifecycle packetization is already explicit
- but no active boundary has yet replaced one host-owned AutoLap execution
  slice

What still needs to happen:

1. choose one narrow runtime slice only:
   - guide-load result consumption
   - route-build result consumption
   - route-step read-only observability
2. replace the equivalent host-local slice in the same patch
3. prove one stable active boundary, or formally leave AutoLap passive for this
   branch

Closure criterion:

- either one narrow active AutoLap boundary is accepted
- or the subsystem is explicitly frozen as passive-only for this branch

Primary docs:

- `AUTO_LAP_BOUNDARY_CLOSURE_CONSOLIDATED.md`
- `AUTO_LAP_ROUTE_REINTRODUCTION_STRATEGY.md`

### 3. Presenter broad closure decision

Status:

- Remaining, but low priority

Why it remains:

- the narrow HUD status-line seam is already active and stable
- broader presenter runtime retries were intentionally rolled back

What still needs to happen:

1. decide whether the current narrow HUD seam is sufficient as the final active
   presenter result for this branch
2. if not sufficient, define one new remove-first presenter retry target

Closure criterion:

- either the current narrow presenter seam is declared final
- or one broader presenter retry is explicitly selected as future work

Primary docs:

- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PRESENTER_BOUNDARY_DECISION_RETRY_BLOCKER.md`

## What does not count as remaining work

These areas are already closed enough and should not be reopened unless a new
runtime goal is intentionally chosen:

- `track-render` wrapper cleanup and active-boundary consolidation
- `memory budget` active-boundary consolidation
- `bootstrap/CD` active-boundary consolidation
- `scheduler/reuse` narrow-seam finalization with broader branch freeze
- broad passive/documental indexing cleanup already completed in this branch

## Recommended order to finish

1. decide `car render` final status:
   - one narrow runtime cut
   - or formal passive-only freeze
2. decide `AutoLap` final status:
   - one narrow runtime cut
   - or formal passive-only freeze
3. declare whether the current presenter narrow seam is final for this branch

## Practical definition of "refactor finished"

The refactor can be considered finished for this branch when:

1. every remaining subsystem above is either:
   - actively consolidated
   - or formally frozen with rationale
2. no unresolved "maybe later" runtime branch remains undocumented
3. the final state is reflected in:
   - `ACTIVE_BOUNDARY_CLOSURE_OVERVIEW.md`
   - `REFACTOR_CONSOLIDATED_FINAL_INDEX.md`
