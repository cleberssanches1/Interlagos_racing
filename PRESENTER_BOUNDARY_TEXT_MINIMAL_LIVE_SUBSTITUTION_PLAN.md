# Presenter Boundary Text Minimal Live Substitution Plan

## Objective

Define the smallest acceptable future live substitution order for the current
presenter-side textual boundary, using only the already narrowed
`PresenterBoundaryTextPacket`.

This plan stays intentionally stricter than a normal refactor because
`src/game_loop_system.hpp` remains a critical runtime host and previous
presenter-facing retries have regressed emulator stability or ISO size.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- no invalid opcode
- no silent close

## Runtime boundaries covered

Only one future local presenter/debug read path is in scope.

Good candidate shape:

- one stack-local packet assembly point
- one local textual presenter helper
- one remove-first replacement of equivalent direct formatting reads

Not in scope for the first retry:

- presenter ownership extraction
- facade resurrection
- frame-end chain resurrection
- HUD policy changes
- scheduler/reuse policy changes

## Narrow packet chain to use

The first live retry must consume only this existing narrowed chain:

1. `PresenterSummaryObservabilityPacket`
2. `PresenterPresencePreviewPacket`
3. `PresenterSummarySchedulerReusePreviewPacket`
4. `PresenterBoundaryPreviewPacket`
5. `PresenterBoundaryViewPacket`
6. `PresenterBoundaryTextPacket`

The first live retry must not reintroduce broader upstream inputs directly:

- `PresenterInputBundle`
- `PresentationDebugBundle`
- `OverlayDebugBundle`
- `ObservabilityDebugBundle`
- direct runtime walking across multiple presenter/debug sources

## Required substitution order

### Step 1 - textual status line only

The first acceptable live retry should target only the compact status line.

Patch shape:

1. assemble `PresenterBoundaryTextPacket` locally
2. consume only `PresentPresenterBoundaryStatusTextPacket(...)`
3. remove equivalent direct local status formatting reads in the same patch
4. keep packet assembly stack-local

Must remain unchanged:

- frame sequencing
- HUD ownership
- overlay ownership
- scheduler/reuse ownership

### Step 2 - textual decision line second

Only after repeated stable runs from Step 1:

1. reuse the same local `PresenterBoundaryTextPacket`
2. consume only `PresentPresenterBoundaryDecisionTextPacket(...)`
3. remove equivalent direct decision/presence formatting reads in the same
   patch

Must remain unchanged:

- presenter decision logic
- frame-end policy
- HUD visibility policy
- scheduler/reuse behavior

Current blocker:

- there is no existing equivalent local host line for this decision surface in
  `src/game_loop_system.hpp`
- so this step is currently blocked by the remove-first rule
- see `PRESENTER_BOUNDARY_DECISION_RETRY_BLOCKER.md`

### Step 3 - combined textual presenter helper third

Only after repeated stable runs from Steps 1 and 2:

1. consume `PresentPresenterBoundaryTextPacket(...)`
2. do so only in the same local textual presentation path
3. remove the equivalent status+decision sibling calls in the same patch

This step should remain local and substitutional, not additive.

## Best first candidate call-site shape

The first future live use should be:

- one local presenter/debug-only helper
- one narrow textual line already considered non-critical
- no ownership transfer

The preferred candidate is a local presenter/debug call site that already:

- prints compact speed/gear/rpm style telemetry, or
- prints compact presenter/debug decision flags

It must not be the first retry for a boundary that also changes:

- VDP submission order
- HUD state ownership
- overlay sequencing
- scheduler/reuse assembly ownership

## Remove-first rule

Each live patch must be substitutional.

That means:

- if `PresenterBoundaryTextPacket` replaces direct formatting reads, those reads
  must be removed in the same patch
- if no equivalent local read is removed, the patch must remain compile-only

## What must not be pulled into the first live boundary

Do not pull these directly into the first live retry:

- `PresenterInputBundle`
- `PresenterFacadePacket`
- removed historical facade/frame-end/HUD preview chains
- broad render aggregates
- broad overlay/debug aggregates
- direct mixed-source formatting logic from multiple host areas

Those structures may remain upstream/off-path, but the first live boundary
should consume only the current narrowed textual path:

- `PresenterBoundaryPreviewPacket`
- `PresenterBoundaryViewPacket`
- `PresenterBoundaryTextPacket`

## Acceptance criteria

Every future live patch in this sequence must keep:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- no frame pacing drift
- no presenter/HUD visibility drift

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
- the boundary needs more than `PresenterBoundaryTextPacket`
- the patch starts moving presenter ownership together with textual substitution

## Current recommendation

Do not attempt the live retry yet.

The safe immediate move remains:

- keep the textual boundary compile-only
- let future runtime work consume only `PresenterBoundaryTextPacket`
- keep the first retry localized to one local presenter/debug helper

The exact first retry candidate is now documented separately in:

- `PRESENTER_BOUNDARY_TEXT_FIRST_STATUS_RETRY_PATCH_PLAN.md`

The current blocker for the next apparent retry is documented in:

- `PRESENTER_BOUNDARY_DECISION_RETRY_BLOCKER.md`
