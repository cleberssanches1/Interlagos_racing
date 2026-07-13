# Scheduler / Reuse Remove-First Budget Recovery Plan

## Objective

Define the next useful work for `scheduler/reuse` after the accepted narrow live
seam, without reopening the frozen broader runtime retry too early.

This document is specifically about recovering structural/code-size budget
before any future live widening.

## Why this subsystem matters

`scheduler/reuse` is the boundary where these concerns meet:

- Master/Slave simulation cadence
- scheduler drain/completion observability
- track producer state
- frame reuse decision/debug output

Because it sits close to frame pacing and authoritative output consumption, even
small additive changes in this area can:

- move the final ISO envelope
- disturb dead-strip behavior
- destabilize emulator boot

So this subsystem cannot be treated like a normal packetization pass.

## Current accepted live seam

The accepted live path is still:

- local `SimulationSchedulerTelemetryViewPacket` assembly
- local `TrackRenderProducerStatePacket` consumption
- one track-only reuse seam through:
  - `TrackReuseRuntimeState`
  - `BuildTrackReuseRuntimeOwnerPacket(...)`
  - `TryPresentTrackReuseObservabilityDebugBundle(...)`

Reference baseline:

- final ISO must remain exactly `4134912`

## What the latest retry proved

Two increasingly narrow simulation-side retries were already tested and
reverted:

1. broader symmetric owner-join retry
   - final ISO: `4136960`
2. narrower decision-line-only retry
   - stripped-path version: `4132864`
   - compatibility-preserving version: `4136960`

This proves:

- the remaining problem is not missing passive modeling
- the remaining problem is envelope sensitivity in the live boundary
- the next move must be remove-first budget recovery, not another direct live
  retry

## Current live chain to shrink

Today the accepted track-only live presentation path effectively walks:

1. `TrackReuseRuntimeState`
2. `FrameReuseRuntimeOwnerPacket`
3. `FrameReuseDomain::ReuseObservabilitySourceOwnerPacket`
4. `GameLoopObservabilityDomain::ReuseObservabilitySourceOwnerPacket`
5. `ReuseObservabilitySourcePacket`
6. `ReuseObservabilitySourceState`
7. `ReuseObservabilityAssemblyInputs`
8. `ReuseObservabilityPacket`
9. `ReuseObservabilityDebugBundle`
10. debug presenter

That hierarchy is structurally valid, but it also shows where budget can still
be recovered before reopening the simulation-side branch.

## Best remove-first targets

The safest next reductions are these, in order.

## Progress snapshot

Already completed in the current branch:

- the live track-only presenter path now skips the extra `SourceState` hop and
  feeds `ReuseObservabilityAssemblyInputs` directly from one local
  `ReuseObservabilitySourcePacket`
- `game_loop_reuse_source_owner_assembler.hpp` is now reduced to the direct
  local owner build from `ReuseObservabilitySourcePacket`
- `game_loop_reuse_source_owner_assembler.hpp` no longer rebuilds a local owner
  packet by round-tripping through the frame-reuse-domain owner assembler
- `game_loop_reuse_runtime_source_assembler.hpp` now maps directly from
  `FrameReuseRuntimeOwnerPacket` into the local source packet without first
  building a domain-level source snapshot
- `game_loop_reuse_runtime_packet_assembler.hpp` no longer keeps the local
  `*OrDefault` wrapper ladder and now builds reuse view packets inline at the
  local observability join
- compat-only shim headers remain in place only to preserve header-smoke
  validation and historical include stability

What still remains from this plan:

- no further structural cleanup is required for the accepted narrow seam in the
  current branch
- the remaining compat-only shim pair is now intentionally frozen as-is because
  it is already minimal and still participates in header-smoke stability
- the next future decision for this subsystem is no longer another local
  cleanup cut; it is whether to remeasure budget and explicitly authorize a new
  narrow simulation-side retry

## Current branch interpretation

For the current branch, this remove-first recovery pass is now considered
complete at the accepted narrow seam.

That means:

- the local owner/source/assembly ladder has already been reduced to the
  smallest practical shape that kept the stable ISO and smoke validation
- no additional helper trimming is required before leaving this micro-boundary
- the remaining broader scheduler/reuse retry stays frozen until a new explicit
  runtime goal reopens it under the existing ISO rules

### 1. Shrink the observability-source adapter ladder

Primary files:

- `src/game_loop_reuse_source_owner_assembler.hpp`
- `src/game_loop_reuse_source_state_assembler.hpp`
- `src/game_loop_reuse_runtime_observability_ops.hpp`

Reason:

- `ReuseObservabilitySourceState` and `ReuseObservabilityAssemblyInputs` carry
  the same pointer trio
- the live path still pays for multiple adapter hops before
  `BuildReuseObservabilityPacket(...)`

Target shape:

- keep the passive contracts
- reduce duplicate adapter steps
- prefer one narrow local conversion path in observability code

### 2. Collapse owner/source forwarding that is only preserving shape

Primary files:

- `src/game_loop_reuse_source_owner_assembler.hpp`
- `src/frame_reuse_runtime_observability_owner_assembler.hpp`
- `src/frame_reuse_observability_capture_ops.hpp`

Reason:

- the tree still contains more than one owner/source forwarding surface for the
  same reuse-origin payload
- this is a good candidate for remove-first cleanup without changing policy

Target shape:

- preserve the future non-critical origin contracts
- remove redundant forwarding glue where the live path already has a narrower
  local owner

### 3. Trim default-wrapper packet builders around reuse views

Primary files:

- `src/game_loop_reuse_runtime_packet_assembler.hpp`

Reason:

- the live track-only seam uses a narrow decision/debug path
- wrapper builders that mainly convert null pointers into empty packets should
  be reviewed for consolidation

Target shape:

- keep semantics unchanged
- remove wrapper duplication before any new live seam widening

### 4. Keep the future simulation-side retry below the owner-join seam

Primary files:

- `src/game_loop_simulation_reuse_runtime_bridge_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_decision_assembler.hpp`
- `src/game_loop_reuse_runtime_seam_assembler.hpp`

Reason:

- the combined owner-join retry already proved too expensive
- any future return should first target the already narrowed decision path, not
  the broader owner aggregate

Target shape:

- recover budget first
- only then retry the simulation-side decision contribution in one local debug
  call site

## Recommended execution order

### Phase A - structural cleanup only

Do first:

1. consolidate the source-state / assembly-input adapter ladder
2. remove redundant owner/source forwarding glue
3. trim wrapper assemblers that only forward to the same packetization path

Do not do yet:

- no new runtime owner joins
- no new simulation-side live seam
- no scheduler policy movement

### Phase B - remeasure fixed envelope

After Phase A:

1. validate stable ISO
2. verify emulator boot remains stable
3. record whether always-live budget actually improved

If budget is not measurably recovered:

- keep the broader branch frozen

Current branch result:

- Phase A is complete
- the branch now stops at the accepted narrow seam instead of reopening Phase C

### Phase C - only then consider one retry

Only after recovered budget is confirmed:

1. retry the narrowest simulation-side decision contribution
2. keep it in one local observability/debug call site
3. remove equivalent local logic in the same patch

## What must not happen

Do not mix this budget-recovery work with:

- track render changes
- presenter widening
- drain/dispatch policy edits
- `N-1` behavior changes
- bootstrap/CD work

The whole point is to isolate binary-layout recovery inside one subsystem.

## Acceptance criteria

The budget-recovery work is acceptable only if it keeps:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- no pacing drift

## Validation ritual

- `tools/validate_saturn_stable_build.ps1`
- recreate `BuildDrop\\passive_header_validation`
- recreate `BuildDrop\\observability_header_validation`
- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

## Related documents

- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`
- `SIMULATION_REUSE_SEAM_COMPLETION_PLAN.md`
- `SCHEDULER_REUSE_BRANCH_FINAL_STATUS.md`
