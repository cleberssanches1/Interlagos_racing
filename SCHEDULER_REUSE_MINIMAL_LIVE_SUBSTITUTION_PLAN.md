# Scheduler / Reuse Minimal Live Substitution Plan

## Objective

Define the smallest acceptable future live substitution order for the
`scheduler/reuse observability` family, without changing scheduling policy,
producer ownership, or frame pacing.

This plan is intentionally more restrictive than a normal refactor plan because
this area is close to Master/Slave timing and previous-frame reuse behavior.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- no invalid opcode
- no silent close

## Current live retry blocker

The first narrow live retry of `Boundary D` was attempted after the compile-only
groundwork was prepared.

Observed result:

- runtime integration shape was technically valid
- final ISO became `4139008`
- baseline delta was exactly `4096` bytes

So the current blocker is code-size budget, not boundary semantics.

Use:

- `DEBUG_TELEMETRY_SIZE_REDUCTION_PLAN.md`

before reopening the first live retry for `Boundary D`.

Latest measured result:

- a decision-first retry was reattempted after the first three size-reduction
  passes
- cumulative telemetry remained excluded
- ISO still became `4139008`
- the `4096` byte blocker remains unchanged

## Current status

- two narrow local live substitutions are now active in `src/game_loop_system.hpp`
- `SimulationSchedulerTelemetryViewPacket` is now assembled in
  `BuildSh2SplitTelemetrySnapshot()`
- `TrackRenderProducerStatePacket` is now consumed in a separate local
  presentation/debug helper without changing `Sh2SplitTelemetrySnapshot`
- the next recommended live candidate is Boundary D from
  `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`

## Runtime boundaries covered

Only future observability-facing call sites are in scope.

Good candidate boundary types:

- one local debug/telemetry assembly point
- one presenter/debug-only read path
- one stack-local packetization point replacing equivalent scattered reads

Not in scope for the first retry:

- dispatch policy changes
- drain behavior changes
- producer kick/fallback changes
- `N-1` ownership changes

## Passive hierarchy to consume

The future live boundary must consume only already-existing passive layers:

1. `SimulationDrainViewPacket`
2. `SimulationCompletionViewPacket`
3. `SimulationSchedulerTelemetryViewPacket`
4. `TrackRenderProducerStatePacket`
5. `ReuseObservabilityPacket`
6. `SimulationSchedulerLifecycleObservabilityPacket`
7. `SchedulerReuseObservabilityPacket`
8. `SchedulerReuseFlowObservabilityPacket`

That means the first live retry must not reintroduce broader upstream state like:

- `SimulationSchedulerTelemetry`
- `FrameReuseTelemetry`
- `SimulationReuseDecisionPacket`
- `TrackReuseDecisionPacket`
- direct runtime-state walking across multiple owners

## Required substitution order

### Step 1 - local scheduler telemetry assembly first

The first acceptable live retry should target only one local observability
assembly point that already reads scheduler counters.

Patch shape:

1. assemble `SimulationSchedulerTelemetryViewPacket` locally
2. consume it only in one debug/telemetry-facing call path
3. remove equivalent local scheduler counter reads in the same patch
4. keep packet assembly stack-local

Must remain unchanged:

- `SimulationRuntimeState` ownership
- dispatch/drain behavior
- lockstep wait logic

### Step 2 - local producer-state assembly second

Only after repeated stable runs from Step 1:

1. reuse the already-existing `TrackRenderProducerStatePacket`
2. consume it only in the same local observability-facing call path or its
   immediate sibling
3. remove equivalent local producer-state reads in the same patch

Must remain unchanged:

- track producer scheduling
- safe mode behavior
- render submission order

### Step 3 - reuse family aggregate third

Only after repeated stable runs from Steps 1 and 2:

1. consume `ReuseObservabilityPacket` in one local debug/presenter-facing
   assembly point
2. replace equivalent scattered reads of:
   - simulation reuse view inputs
   - track reuse view inputs
3. keep the packet local to the same scope

Must remain unchanged:

- reuse ownership
- `N-1` policy behavior
- simulation/track orchestration

Preferred compile-only staging directly above Boundary D:

- `src/game_loop_reuse_observability_debug_contracts.hpp`
- `src/game_loop_reuse_observability_debug_assembler.hpp`
- `src/game_loop_reuse_observability_debug_presenter_ops.hpp`
- `src/game_loop_reuse_observability_debug_bundle_contracts.hpp`
- `src/game_loop_reuse_observability_debug_bundle_assembler.hpp`
- `src/game_loop_reuse_observability_debug_bundle_presenter_ops.hpp`

Current narrowing status for that staging:

- `ReuseObservabilityDebugPacket` is now decision-only
- cumulative reuse counters stay outside the first Boundary D live retry
- the first live return should therefore consume only:
  - simulation reuse decision flags
  - track reuse decision flags

### Step 4 - scheduler/reuse aggregate last

Only after the lower layers have each been proven stable independently:

1. consume `SchedulerReuseObservabilityPacket`
2. do so in one local observability/presenter-only path
3. remove the lower-level equivalent reads in the same patch

This step should only happen when:

- the lower-level packet consumers are already stable
- the integration is substitutional, not additive
- no scheduling behavior moves in the same patch

### Step 5 - flow aggregate only after lower observability is proven

Only after the lifecycle and scheduler/reuse layers have each been proven
stable independently:

1. consume `SchedulerReuseFlowObservabilityPacket`
2. do so only in one observability-facing or presenter-facing read boundary
3. remove the equivalent lower-level assembly/read composition in the same patch

This step should only happen when:

- the consumer truly needs lifecycle and reuse reasoning together
- no dispatch/drain/producer behavior moves in the same patch
- the patch is still substitutional, not additive

## Best first candidate call-site shape

The first future live use should be:

- one stack-local observability assembly helper
- one debug/presenter-only consumer
- no ownership transfer

The best candidate is whichever call site:

- already reads scheduler counters
- already reads track producer status
- does not influence dispatch or render policy

Good compile-only follow-up work before that retry:

- keep `SimulationDrainViewPacket` and `SimulationCompletionViewPacket`
  lifecycle-oriented and narrow
- prefer `SimulationSchedulerLifecycleObservabilityPacket` for any future
  scheduler-only observability consumer before mixing it with reuse
- prefer `src/game_loop_scheduler_reuse_observability_assembly_ops.hpp` to
  assemble the lower-to-higher chain off-path before any new live retry

## Remove-first rule

Each live patch must be substitutional.

That means:

- if a packet field replaces a local read, the local read must be removed in the
  same patch
- if the patch adds packet assembly but does not remove equivalent reads, it
  should remain compile-only

## What must not be pulled into the first live boundary

Do not pull these directly into the first live retry:

- `SimulationDispatchPacket`
- `SimulationDrainPacket`
- `SimulationFrameContext`
- `TrackRenderFramePacket`
- direct `SimulationRuntimeState` orchestration
- direct `TrackSystem` producer control

Those structures may remain upstream/off-path, but the first live boundary
should consume only the already narrowed passive packets.

## Acceptance criteria

Every future live patch in this sequence must keep:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- no frame pacing drift
- no lockstep/drain regressions
- no producer-safe-mode regressions

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
- live packetization expands into scheduling policy code
- packet assembly becomes additive instead of substitutional
- observability integration starts moving producer/scheduler ownership

## Related documents

- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
- `PASSIVE_TO_RUNTIME_INTEGRATION_PLAN.md`
