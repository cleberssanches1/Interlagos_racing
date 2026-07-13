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

There is no mandatory remaining subsystem work left for refactor closure in
this branch.

Any further runtime move now counts as a new explicit goal, not as required
closure debt.

## What does not count as remaining work

These areas are already closed enough and should not be reopened unless a new
runtime goal is intentionally chosen:

- `track-render` wrapper cleanup and active-boundary consolidation
- `memory budget` active-boundary consolidation
- `bootstrap/CD` active-boundary consolidation
- `car render` narrow runtime-seam finalization with broader branch defer
- `presenter` narrow-seam finalization with broader branch defer
- `scheduler/reuse` narrow-seam finalization with broader branch freeze
- broad passive/documental indexing cleanup already completed in this branch

## Recommended order to finish

1. keep current closed boundaries stable
2. only reopen runtime when a new explicit goal justifies it

## Practical definition of "refactor finished"

The refactor can be considered finished for this branch when:

1. every subsystem formerly listed as remaining is either:
   - actively consolidated
   - or formally frozen with rationale
2. no unresolved "maybe later" runtime branch remains undocumented
3. the final state is reflected in:
   - `ACTIVE_BOUNDARY_CLOSURE_OVERVIEW.md`
   - `REFACTOR_CONSOLIDATED_FINAL_INDEX.md`
