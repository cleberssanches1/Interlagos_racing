# Presenter Summary Scheduler/Reuse Simulation Minimal Live Substitution Plan

## Objective

Define the smallest acceptable future live substitution order for the new
presenter/resumo simulation-side scheduler/reuse boundary, using only the
already prepared narrow compile-only packets and preserving emulator stability.

This plan stays intentionally strict because:

- `src/game_loop_system.hpp` remains the critical runtime host
- the first broader simulation-side live retry already regressed emulator boot
- the new boundary is currently stronger as a compile-only attach surface than
  as an immediate runtime target

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
- one remove-first replacement of equivalent direct local formatting reads

Not in scope for the first retry:

- reopening `TryBuildReuseObservabilityDebugBundle(...)`
- adding simulation-side live history commit and presentation together
- reopening combined `FrameReuseRuntimeOwnerPacket` assembly
- moving scheduler/reuse ownership
- changing drain or dispatch behavior

## Narrow packet chain to use

The first future live retry must consume only this existing narrowed chain:

1. `SimulationReuseRuntimeDebugPreviewPacket`
2. `SchedulerReuseSimulationPreviewPacket`
3. `PresenterSummarySchedulerReuseSimulationPreviewPacket`
4. `PresenterSummarySchedulerReuseSimulationViewPacket`
5. `PresenterSummarySchedulerReuseSimulationTextPacket`

The first live retry must not pull broad upstream inputs directly back into the
host path:

- `FrameReuseDomain::FrameReuseRuntimeOwnerPacket`
- `ReuseObservabilityPacket`
- `SchedulerReuseObservabilityPacket`
- `SchedulerReuseFlowObservabilityPacket`
- `PresenterInputBundle`
- mixed direct runtime reads across scheduler/reuse sources

## Required substitution order

### Step 1 - status text only

The first acceptable live retry should target only the compact textual status
surface produced by:

- `PresentPresenterSummarySchedulerReuseSimulationStatusTextPacket(...)`

Patch shape:

1. assemble the narrow preview chain locally
2. consume only the status text helper
3. remove equivalent direct local status formatting reads in the same patch
4. keep packet assembly stack-local

Must remain unchanged:

- frame sequencing
- HUD ownership
- overlay ownership
- scheduler/reuse ownership
- simulation drain/dispatch behavior

### Step 2 - combined text helper second

Only after repeated stable runs from Step 1:

1. reuse the same local chain
2. consume `PresentPresenterSummarySchedulerReuseSimulationTextPacket(...)`
3. remove the equivalent sibling status helper call in the same patch

This step must remain local and substitutional, not additive.

## Current blocker

At the moment, this path does not have an identified equivalent direct local
host line in `src/game_loop_system.hpp` that is clearly safe to replace.

That means:

- the boundary is ready
- the future attach point is narrower
- but the first live retry is still blocked by the remove-first rule until that
  exact local line is selected

## Best first candidate call-site shape

The first future live use should be:

- one local presenter/debug-only helper
- one narrow textual line already treated as non-critical
- no ownership transfer

The best candidate is a path that already prints compact:

- scheduler/reuse debug flags, or
- simulation-side textual debug status

It must not be the first retry for a boundary that also changes:

- VDP submission order
- HUD state ownership
- overlay sequencing
- scheduler/reuse assembly ownership

## Remove-first rule

Each live patch must be substitutional.

That means:

- if this chain replaces direct formatting reads, those reads must be removed in
  the same patch
- if no equivalent local read is removed, the patch must remain compile-only

## Acceptance criteria

Every future live patch in this sequence must keep:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- no frame pacing drift
- no scheduler/reuse observability drift

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
- the patch needs broader packets than the chain above
- runtime ownership starts moving together with textual substitution

## Current recommendation

Do not attempt the live retry yet.

The safe immediate move remains:

- keep this boundary compile-only
- treat it as the future attach point for the smallest simulation-side textual
  retry
- wait for one explicit remove-first local presenter/debug candidate

## Related documents

- `PRESENTER_SUMMARY_SCHEDULER_REUSE_SIMULATION_BOUNDARY_CONSOLIDATED.md`
- `SIMULATION_REUSE_LIVE_RETRY_BLOCKER.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
