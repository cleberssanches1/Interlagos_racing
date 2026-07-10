# Simulation Reuse Seam Completion Plan

## Objective

Describe exactly what is still missing to complete the simulation-side branch at
the already accepted local reuse seam in `src/game_loop_system.hpp`.

This document is runtime-facing planning only.

It does not authorize a live patch by itself.

## Current accepted seam

The local reuse seam already accepted live is:

- `GameLoopSystem::TryBuildReuseObservabilityDebugBundle(...)`

Current accepted behavior:

- the seam already receives a real track-only
  `FrameReuseRuntimeOwnerPacket`
- that packet is built from `TrackReuseRuntimeState`
- simulation-side reuse is still neutral there

So the remaining work is not “reopen Boundary D from scratch”.

It is:

- complete only the missing simulation-side branch in the same seam

## Exact missing branch

The missing branch is the simulation half of:

- `SimulationFrameHistoryState`
- `SimulationReuseDecisionPacket`

feeding the already prepared passive chain:

- `SimulationReuseDecisionViewPacket`
- `SimulationReuseTelemetryViewPacket`
- `ReuseObservabilityPacket`

## Smallest persistent state still missing

The smallest missing long-lived runtime state is:

- `FrameReuseDomain::SimulationFrameHistoryState`

Why only this is required:

- `requestFrameId` already exists as `frameCounter_`
- `slaveSimulationEnabled` already exists in the local host context
- `lockstepEnabled` already exists in the local host context
- `jobInFlight` already exists as `simState_.JobInFlight()`

So unlike the accepted track-side cut, the simulation-side branch does not
obviously need a second broad runtime-state mirror first.

The minimum safe shape appears to be:

1. persist only `SimulationFrameHistoryState`
2. derive `SimulationReuseDecisionPacket` stack-locally at the seam
3. keep cumulative simulation reuse telemetry out of the first live retry

## Exact local inputs already available

At the host seam, the simulation-side decision can already be derived from:

- `frameCounter_`
- `context_.EnableSlaveForSimulation()`
- `context_.SlaveSimulationLockstep()`
- `simState_.JobInFlight()`
- committed `SimulationFrameHistoryState`

using the existing passive helper:

- `FrameReuseDomain::SeedSimulationReuseDecisionPacket(...)`

## Exact missing commit points

What is still missing is not the decision model.

What is missing is authoritative commit of simulation history at the exact
points where Slave-produced simulation output becomes authoritative to the host.

Those points are currently:

### 1. `DrainSimulationJobIfInFlight(...)`

Current host behavior there:

1. mark simulation completed
2. read `simState_.output[simState_.completedIdx]`
3. call `ApplySimulationOutput(...)`

This is one authoritative commit point for simulation reuse history.

### 2. `ConsumeCompletedJobs()`

Current host behavior there:

1. build `SimulationCompletionPacket`
2. if `hasCompleted`, read `simState_.output[completionPacket.completedIdx]`
3. call `ApplySimulationOutput(...)`

This is the other authoritative host consumption point for already-completed
Slave simulation output.

## Safe commit rule

The first simulation-side live completion should commit history only when all
of these are true:

1. the frame came from the Slave simulation path
2. that output is becoming authoritative to the host
3. the commit happens in the same local flow that already applies the output

That keeps the retry substitutional and avoids introducing a second parallel
notion of “committed simulation frame”.

## What should not be committed first

Do not commit these in the first simulation-side retry:

- synchronous fallback frames from `RunGameplayFrameSynchronously(...)`
- speculative dispatch inputs
- incomplete in-flight simulation payloads
- cumulative simulation reuse telemetry

Reason:

- the first live completion should mirror only the already accepted track-side
  narrow cut
- synchronous fallback semantics should stay outside the first retry to avoid
  mixing reuse policy with fallback policy

## Minimal accepted patch shape

The smallest acceptable next patch above the current seam should be:

1. add only one narrow persistent member:
   - `FrameReuseDomain::SimulationFrameHistoryState`
2. commit that history only at authoritative Slave-output consumption points
3. derive `SimulationReuseDecisionPacket` stack-locally inside
   `TryBuildReuseObservabilityDebugBundle(...)`
4. build one runtime owner packet carrying:
   - existing track-side branch
   - new simulation-side history
   - new simulation-side decision
5. keep cumulative telemetry still passive/off-path

## Remove-first interpretation

This retry is valid only if it replaces the remaining neutral simulation-side
state in the same seam.

That means:

- no second parallel reuse presentation path
- no new broad scheduler/reuse aggregate consumer first
- no retry that adds simulation reuse while leaving an equivalent neutral branch
  side-by-side

## What must remain unchanged

- scheduler dispatch policy
- lockstep drain behavior
- fallback behavior
- track producer behavior
- final HUD/debug ordering
- track-only live seam already accepted

## Acceptance criteria

The next simulation-side seam completion passes only if:

- ISO remains `4134912`
- emulator still boots
- no invalid opcode
- no silent close
- no lockstep/drain behavior drift
- no duplicate application of completed simulation output
- no broad scheduler/reuse ownership move

## Validation ritual

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`
- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

## Recommended next preparation before code

Before touching runtime, document or extract one tiny passive helper for:

- committing `SimulationFrameHistoryState` from authoritative Slave output

This keeps the next runtime patch smaller and makes the commit points explicit
before any live substitution.

That helper now exists as:

- `src/game_loop_simulation_reuse_runtime_commit_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_commit_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_commit_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_state_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_state_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_decision_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_decision_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_bridge_assembler.hpp`

Current role:

- define one narrow passive commit packet:
  - `SimulationReuseRuntimeCommitPacket`
- build that packet from one authoritative `Game::SimulationPayload`
- accept one authoritative `Game::SimulationPayload`
- commit only its `frameState` into `SimulationFrameHistoryState`
- persist only `SimulationFrameHistoryState` inside one narrow
  `SimulationReuseRuntimeState`
- expose one narrow owner-packet builder above that state for the future seam
- keep the future runtime patch free of repeated commit boilerplate at the two
  authoritative Slave-output consumption points

One sibling passive decision path now also exists for the same future seam:

- `SimulationReuseRuntimeDecisionInputsPacket`
- `BuildSimulationReuseRuntimeDecisionPacket(...)`
- `BuildSimulationReuseRuntimeDecisionViewPacket(...)`

Current role:

- capture exactly the future simulation-side decision inputs already available
  in the host
- derive `SimulationReuseDecisionPacket` passively, outside the runtime seam
- expose the already narrowed `SimulationReuseDecisionViewPacket` directly above
  that same derivation

One narrower compile-only preview now also exists directly above that same
decision path:

- `src/game_loop_simulation_reuse_runtime_preview_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_presenter_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_bridge_presenter_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_bridge_presenter_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_presenter_ops.hpp`
- `src/game_loop_scheduler_reuse_simulation_preview_contracts.hpp`
- `src/game_loop_scheduler_reuse_simulation_preview_assembler.hpp`
- `src/game_loop_scheduler_reuse_simulation_preview_presenter_ops.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_preview_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_preview_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_view_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_view_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_presenter_ops.hpp`

Current role:

- keep the next retry below the combined owner-packet seam
- validate exact host decision inputs plus derived decision view together
- keep one ready-to-use debug presentation helper above that preview only
- keep one ready-to-use bridge from exact host inputs straight into that helper
- keep one ready-to-use local observability/debug wrapper above that bridge
- keep one explicit local debug preview boundary packet above that wrapper
- keep one higher compile-only scheduler/reuse aggregate above that boundary
- keep one sibling presenter helper above that higher aggregate
- keep one presenter/resumo aggregate, view/text, and text presenter above that
  scheduler/reuse preview
- preserve a smaller reopen path after the failed broader live retry
