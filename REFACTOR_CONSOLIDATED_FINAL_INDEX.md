# Refactor Consolidated Final Index

## Objective

Provide one final consolidated index of what has already been refactored,
packetized, narrowed, or safely integrated in the current branch.

This document is the top-level navigation entry for the refactor state.

It does not authorize new runtime substitutions by itself.

Operational next-cuts companion:

- `REFACTOR_RUNTIME_SAFE_NEXT_CUTS.md`
- `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`
- `REFACTOR_CLOSURE_EXECUTION_PLAN.md`
- `GAME_LOOP_RUNTIME_CRITICAL_ENGINEERING_POLICY.md`
- `ACTIVE_BOUNDARY_CLOSURE_OVERVIEW.md`
- `REFACTOR_FINAL_REMAINING_WORK.md`

## Stable baseline

- target ISO remains `4134912`
- emulator boot must remain stable
- `AUDIO_PROFILE=1`
- runtime-critical policy reference:
  - `GAME_LOOP_RUNTIME_CRITICAL_ENGINEERING_POLICY.md`
- critical runtime ownership remains centered on:
  - `src/game_loop_system.hpp`
  - `src/main.cxx`
  - `src/track_system.hpp`
  - `src/track_system.cxx`
  - `src/car_system.hpp`
  - `src/car_system.cxx`

## What has already been achieved

The refactor already established five concrete outcomes:

1. broad packetization groundwork now exists across presenter, track render,
   scheduler/reuse, memory budget, bootstrap/CD, car render, and AutoLap
2. several narrow live substitutions are already accepted and stable
3. risky domains now have explicit passive boundaries and minimal live plans
4. multiple compile-only preview chains were created, used to drive safe
   remove-first cleanup, and then removed once they became fully orphaned
5. documentation now reflects both active live cuts and blocked retry paths

## Current accepted live runtime cuts

These are the live substitutions currently considered stable.

### Scheduler / reuse observability

- local `SimulationSchedulerTelemetryViewPacket` assembly is live
- local `TrackRenderProducerStatePacket` consumption is live
- one track-only reuse seam is live through:
  - local `TrackReuseRuntimeState`
  - on-demand real `FrameReuseRuntimeOwnerPacket`
  - `TryBuildReuseObservabilityDebugBundle(...)`

Key references:

- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `SIMULATION_REUSE_SEAM_COMPLETION_PLAN.md`

### Track render

- live narrow path for overlay query metrics
- live SH2 telemetry snapshot path
- live producer in-flight hint path
- one narrow shared `TrackRenderTelemetryViewPacket` feed is live in the
  presentation/HUD path
- one local `TrackRenderSh2PresentationPacket` HUD/debug path is live

Key references:

- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `SH2_TRACK_RENDER_PRESENTATION_BOUNDARY_CONSOLIDATED.md`

### Memory budget

- narrow runtime-bridge category-policy accessors are live
- `MemoryDebugPresentationBundle` live local consumer is active in
  `UpdateFrameEndOverlays()`
- `MemoryDebugPresentationBundle` live local consumer is active in
  `UpdateLowWorkFreeOverlayEnabled()`

Key references:

- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `MEMORY_BUDGET_RUNTIME_BRIDGE_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_LOW_WORK_OVERLAY_BOUNDARY_CONSOLIDATED.md`

### Presenter

- HUD status-line path is already live through:
  - `DrivingHudTextPacket`
  - `PresenterBoundaryStatusTextPacket`
  - `PresentPresenterBoundaryHudStatusTextPacket(...)`

Current branch-final point:

- the narrow HUD status-text seam is treated as the final active presenter
  result for this branch
- broader presenter retries are intentionally deferred instead of kept as
  mandatory remaining work

Key references:

- `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`
- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PRESENTER_BRANCH_FINAL_STATUS.md`

## Current compile-only but structurally prepared boundaries

These families are already extracted and validated, but remain intentionally
non-live.

### Presenter / observability

Current state:

- the broad presenter compile-only preview hierarchy was removed after smoke
  validation stopped depending on it
- the surviving active seam is now only the HUD status-line path

Important note:

- older `PresenterFacade*`, `PresenterFrameEndDecision*`, and
  `PresenterHudTelemetryDecision*` documentation remains useful as historical
  planning, but those source headers are no longer the active top boundary in
  this branch

Current highest active source-side presenter boundary:

- `PresenterBoundaryStatusTextPacket`

Current branch-final interpretation:

- the narrow live HUD seam is sufficient for this branch
- broader presenter runtime widening is deferred, not pending mandatory closure

Key references:

- `PRESENTER_PASSIVE_REFACTOR_INVENTORY.md`
- `PRESENTER_SUMMARY_SCHEDULER_REUSE_SIMULATION_BOUNDARY_CONSOLIDATED.md`
- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PRESENTER_BRANCH_FINAL_STATUS.md`

### Scheduler / reuse passive chain

Established layers:

- simulation drain/completion/scheduler telemetry views
- track and simulation reuse decision/telemetry views
- `ReuseObservabilityPacket`
- `SimulationSchedulerLifecycleObservabilityPacket`
- `SchedulerReuseObservabilityPacket`
- `SchedulerReuseFlowObservabilityPacket`
- reduced passive owner/source/debug-bundle assembly around the accepted
  track-only seam

Recent cleanup:

- the former track-reuse preview ladder was removed after it became fully
  orphaned from `src/` and smoke validation
- the former simulation-reuse preview/debug ladder was removed for the same
  reason
- the former narrow `ReuseRuntimePreviewPacket` contract was also removed once
  the remaining path was reduced to the owner/source/observability layers that
  still exist in the tree
- the former scheduler/reuse assembly-ops bridge leaf was also removed once the
  remaining path no longer needed a dedicated wrapper above the aggregate
  assemblers
- the former minimal `SchedulerReuseDebugTelemetry` assembler leaf was removed
  after it became observability-smoke-only and the contract remained sufficient

Current branch-final point:

- the narrow accepted seam is treated as the final active result for this
  branch
- the broader symmetric simulation-side retry is formally frozen by emulator
  stability and code-size pressure under the fixed ISO envelope

Key references:

- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`
- `SIMULATION_REUSE_RUNTIME_DEBUG_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SCHEDULER_REUSE_SIMULATION_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SIMULATION_REUSE_LIVE_RETRY_BLOCKER.md`
- `SCHEDULER_REUSE_BRANCH_FINAL_STATUS.md`

### Track render

Established layers:

- full frame packet
- reduced debug packet
- telemetry view packet
- producer-state packet
- producer-hint packet
- SH2 presentation packet
- presentation/observability aggregate packet
- live consumer now talks directly to the SH2 presentation packet assembler
  without an extra runtime bridge leaf

Recent cleanup:

- the former `game_loop_track_render_sh2_presentation_runtime_assembler.hpp`
  bridge leaf was removed after its only consumer absorbed the forwarding logic
  directly
- the former `game_loop_track_render_debug_assembler.hpp` wrapper was removed
  after its only consumer absorbed the packet assembly directly
- the former `game_loop_track_render_producer_hint_assembler.hpp` wrapper was
  removed after its only consumer absorbed the packet assembly directly

Key references:

- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `TRACK_RENDER_PRESENTATION_BOUNDARY_CONSOLIDATED.md`
- `SH2_TRACK_RENDER_PRESENTATION_BOUNDARY_CONSOLIDATED.md`
- `TRACK_RENDER_ACTIVE_CONTRACTS_BOUNDARY_CONSOLIDATED.md`

### Car render

Established layers:

- car visual frame packet
- shadow packet assembly
- shadow-prep preview packet
- render/submit packet separation groundwork

Current constrained point:

- broad `CarVisualFramePacket` runtime integration remains deferred by binary
  budget

Current branch-final point:

- the narrow render-packet plus shadow-prep plus sync plus submit runtime seam
  is treated as the final active `car render` result for this branch
- broader visual/runtime packet handoff is intentionally deferred instead of
  kept as mandatory remaining work

Key references:

- `CAR_RENDER_PASSIVE_FLOW_PLAN.md`
- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`
- `CAR_RENDER_BOUNDARY_CLOSURE_CONSOLIDATED.md`
- `CAR_RENDER_BRANCH_FINAL_STATUS.md`

### CD asset / bootstrap

Established layers:

- CD asset frame packet
- bootstrap decision packet
- SBA bootstrap decision packet
- anchor bootstrap decision packet
- bootstrap decision bridge packet

Current runtime posture:

- bootstrap ownership remains in `src/main.cxx`

Key references:

- `CD_ASSET_PASSIVE_FLOW_PLAN.md`
- `CD_ASSET_MINIMAL_BOOTSTRAP_SUBSTITUTION_PLAN.md`
- `CD_BOOTSTRAP_CHAIN_FLOW_PLAN.md`
- `CD_ASSET_ACTIVE_CONTRACTS_BOUNDARY_CONSOLIDATED.md`

### Memory budget

Established layers:

- category-policy bridge surfaces
- memory snapshot/pressure/policy/telemetry packets
- render-budget observability narrowing
- frame-end and low-work overlay presentation boundaries
- high-work and low-work trace text packetization

Recent cleanup:

- former memory-budget frame packet layer was removed once it became smoke-only
- former render-budget presentation/overlay text chain was removed once it
  became smoke-only

Key references:

- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_FRAME_END_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_ACTIVE_CONTRACTS_BOUNDARY_CONSOLIDATED.md`

### AutoLap route

Established layers:

- AutoLap frame packet
- guide-line/segment/route packet slices
- route-building passive helpers

Current runtime posture:

- host ownership still remains local for the broader AutoLap flow
- broader runtime moves are frozen for this branch by the fixed ISO envelope

Current branch-final point:

- the narrow helper seam through lifecycle/build helpers and route-trace
  assembly is treated as the final active `AutoLap` result for this branch
- broader route/runtime packet handoff is frozen instead of kept as mandatory
  remaining work

Key references:

- `AUTO_LAP_ROUTE_PASSIVE_FLOW_PLAN.md`
- `AUTO_LAP_ROUTE_REINTRODUCTION_STRATEGY.md`
- `AUTO_LAP_BOUNDARY_CLOSURE_CONSOLIDATED.md`
- `AUTO_LAP_BRANCH_FINAL_STATUS.md`

## What was reduced or stabilized structurally

These improvements matter even where runtime ownership has not moved yet.

- critical host logic is now surrounded by explicit passive contracts
- multiple broad debug/presenter paths were narrowed into view/text packets
- scheduler/reuse retry work is now split into local boundaries instead of one
  monolithic seam
- memory-debug presentation is already localized through bundle consumers
- track-render presentation now has narrower SH2-facing boundaries
- simulation-side retry blockers are documented instead of repeatedly retried
- dead legacy helpers that no longer participate in `src/` runtime or smoke
  validation are being removed instead of preserved as accidental side APIs
  blindly

## Current blockers already understood

### Scheduler / reuse

- the broader symmetric simulation-side retry still reproduces code-size
  pressure around `+4096` bytes
- emulator stability regresses when the retry is widened too early
- the remaining simulation-side completion branch is now under formal freeze
  until remove-first recovery opens at least `2048` bytes of always-live
  budget for the narrow seam join retry, or the ISO strategy changes

Primary blocker docs:

- `SIMULATION_REUSE_LIVE_RETRY_BLOCKER.md`
- `DEBUG_TELEMETRY_SIZE_REDUCTION_PLAN.md`

### Presenter

- first runtime presenter move was rolled back after emulator instability
- presenter runtime ownership remains intentionally local

Primary blocker docs:

- `GAME_LOOP_PRESENTER_RUNTIME_ALTERNATIVES.md`
- `PRESENTER_BOUNDARY_DECISION_RETRY_BLOCKER.md`

### Car render

- binary budget still blocks the broader live move

### Bootstrap / CD

- runtime bootstrap sequencing remains too sensitive for a broad first move

## Runtime follow-up policy

There is no branch-mandatory next runtime priority anymore.

The branch is already structurally closed.

Any new runtime move must now be treated as:

- a new explicit goal
- boundary-local
- remove-first
- validated against the fixed ISO `4134912`

That means the old “what should come next” ordering is no longer part of the
mandatory refactor closure state.

## Validation contract already standardized

Compile-only validation:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

Stable build validation:

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

Rule:

- any future runtime move must be remove-first, boundary-local, and keep the
  ISO at `4134912`
- use `GAME_LOOP_RUNTIME_CRITICAL_ENGINEERING_POLICY.md` as the explicit
  interpretation guide for method size, helper growth, and binary-budget
  decisions in critical hosts

## Recommended reading order

If someone needs to resume this refactor later, the shortest reliable path is:

1. `REFACTOR_CONSOLIDATED_FINAL_INDEX.md`
2. `PASSIVE_CONTRACTS_INVENTORY.md`
3. the subsystem inventory/plan for the target boundary
4. the matching minimal live substitution plan
5. only then the critical host file that would consume that boundary

## Canonical subsystem entry points

Use these as the main entry docs by subsystem:

- Presenter: `PRESENTER_PASSIVE_REFACTOR_INVENTORY.md`
- Track Render: `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- Scheduler/Reuse: `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- Memory Budget: `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- CD/Bootstrap: `CD_ASSET_PASSIVE_FLOW_PLAN.md`
- Car Render: `CAR_RENDER_PASSIVE_FLOW_PLAN.md`
- AutoLap: `AUTO_LAP_ROUTE_PASSIVE_FLOW_PLAN.md`

## Final state summary

The refactor is no longer at the stage of discovering shapes.

The project now has:

- explicit passive boundaries
- explicit live accepted cuts
- explicit blocked retry points
- explicit records of which compile-only ladders were removed versus which
  passive boundaries still remain
- explicit subsystem inventories to resume work safely

The remaining work for branch closure is exhausted.

Any further move is now a fresh runtime goal, not unresolved structural
discovery for this refactor branch.
