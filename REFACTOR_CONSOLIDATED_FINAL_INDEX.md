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

## Stable baseline

- target ISO remains `4134912`
- emulator boot must remain stable
- `AUDIO_PROFILE=1`
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
4. multiple compile-only preview chains now exist above critical runtime seams
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

Key references:

- `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`
- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

## Current compile-only but structurally prepared boundaries

These families are already extracted and validated, but remain intentionally
non-live.

### Presenter / observability

Established layers:

- presentation bundles
- render debug bundles
- overlay/observability bundles
- presenter summary boundary
- presenter presence decision/preview boundary
- presenter top boundary preview/view/text
- presenter scheduler/reuse preview chain
- presenter summary scheduler/reuse simulation preview/view/text chain

Important note:

- older `PresenterFacade*`, `PresenterFrameEndDecision*`, and
  `PresenterHudTelemetryDecision*` documentation remains useful as historical
  planning, but those source headers are no longer the active top boundary in
  this branch

Current highest active source-side presenter boundary:

- `PresenterSummaryObservabilityPacket`

Key references:

- `PRESENTER_PASSIVE_REFACTOR_INVENTORY.md`
- `PRESENTER_SUMMARY_SCHEDULER_REUSE_SIMULATION_BOUNDARY_CONSOLIDATED.md`
- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

### Scheduler / reuse passive chain

Established layers:

- simulation drain/completion/scheduler telemetry views
- track and simulation reuse decision/telemetry views
- `ReuseObservabilityPacket`
- `SimulationSchedulerLifecycleObservabilityPacket`
- `SchedulerReuseObservabilityPacket`
- `SchedulerReuseFlowObservabilityPacket`
- simulation reuse runtime debug preview boundary
- scheduler/reuse simulation preview join boundary
- presenter/resumo simulation preview/view/text chain above that join

Current blocked point:

- broader symmetric simulation-side live retry is still blocked by emulator
  stability and code-size pressure

Key references:

- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`
- `SIMULATION_REUSE_RUNTIME_DEBUG_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SCHEDULER_REUSE_SIMULATION_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SIMULATION_REUSE_LIVE_RETRY_BLOCKER.md`

### Track render

Established layers:

- full frame packet
- reduced debug packet
- telemetry view packet
- producer-state packet
- producer-hint packet
- SH2 presentation packet
- presentation/observability aggregate packet
- compile-only previews above current live seams

Key references:

- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `TRACK_RENDER_PRESENTATION_BOUNDARY_CONSOLIDATED.md`
- `SH2_TRACK_RENDER_PRESENTATION_BOUNDARY_CONSOLIDATED.md`

### Car render

Established layers:

- car visual frame packet
- shadow packet assembly
- shadow-prep preview packet
- render/submit packet separation groundwork

Current constrained point:

- broad `CarVisualFramePacket` runtime integration remains deferred by binary
  budget

Key references:

- `CAR_RENDER_PASSIVE_FLOW_PLAN.md`
- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`

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

### Memory budget

Established layers:

- category-policy bridge surfaces
- memory snapshot/pressure/policy/telemetry packets
- render-budget observability/presentation/overlay text narrowing
- frame-end and low-work overlay presentation boundaries
- high-work and low-work trace text packetization

Key references:

- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_FRAME_END_BOUNDARY_CONSOLIDATED.md`

### AutoLap route

Established layers:

- AutoLap frame packet
- guide-line/segment/route packet slices
- route-building passive helpers

Current runtime posture:

- remains mostly passive
- future runtime moves must remain substitutional

Key references:

- `AUTO_LAP_ROUTE_PASSIVE_FLOW_PLAN.md`
- `AUTO_LAP_ROUTE_REINTRODUCTION_STRATEGY.md`

## What was reduced or stabilized structurally

These improvements matter even where runtime ownership has not moved yet.

- critical host logic is now surrounded by explicit passive contracts
- multiple broad debug/presenter paths were narrowed into view/text packets
- scheduler/reuse retry work is now split into local boundaries instead of one
  monolithic seam
- memory-debug presentation is already localized through bundle consumers
- track-render presentation now has narrower SH2-facing boundaries
- simulation-side retry blockers are documented instead of repeatedly retried
  blindly

## Current blockers already understood

### Scheduler / reuse

- the broader symmetric simulation-side retry still reproduces code-size
  pressure around `+4096` bytes
- emulator stability regresses when the retry is widened too early

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

## Validation contract already standardized

Compile-only validation:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

Stable build validation:

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

Rule:

- any future runtime move must be remove-first, boundary-local, and keep the
  ISO at `4134912`

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
- explicit compile-only preview ladders above critical seams
- explicit subsystem inventories to resume work safely

The remaining work is mostly controlled runtime substitution, not structural
discovery.
