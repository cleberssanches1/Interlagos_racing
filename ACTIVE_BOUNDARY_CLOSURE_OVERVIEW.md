# Active Boundary Closure Overview

## Objective

Provide one final cross-subsystem overview of the active boundaries that are
already considered structurally consolidated in the current branch.

This document is inventory-only.

It does not authorize new runtime substitutions by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the critical host for frame/runtime
  presentation
- `src/main.cxx` remains the critical host for bootstrap ownership
- no cross-subsystem merge should widen ownership in one patch

## Boundary status legend

- **Active consolidated**: live call sites exist and the active boundary shape
  is already documented as stable
- **Passive/documental**: packet chains exist, but runtime ownership is still
  intentionally local or deferred
- **Blocked**: broader live retry path is understood but currently frozen

## Subsystem closure map

### 1. Track Render

Status:

- Active consolidated

Primary active boundary doc:

- `TRACK_RENDER_ACTIVE_CONTRACTS_BOUNDARY_CONSOLIDATED.md`

Current live entry points:

- `src/game_loop_system.hpp`
- `src/game_loop_track_render_runtime_observability_ops.hpp`
- `src/game_loop_render_debug_assembler.hpp`

Current live surfaces:

- `TrackRenderTelemetryViewPacket`
- `TrackRenderProducerHintPacket`
- `TrackRenderSh2PresentationPacket`
- `TrackRenderDebugPacket`

What stays outside:

- producer/sort ownership
- track scheduling policy
- safe-mode policy
- `N-1` reuse behavior

### 2. Memory Budget

Status:

- Active consolidated

Primary active boundary doc:

- `MEMORY_BUDGET_ACTIVE_CONTRACTS_BOUNDARY_CONSOLIDATED.md`

Current live entry points:

- `src/game_loop_system.hpp`
- `src/main.cxx`
- `src/car_audio_system.hpp`

Current live surfaces:

- `MemoryBudgetRuntimeBridge::*`
- `MemoryDebugPresentationBundle`
- low-work overlay text/bundle path
- high/low work trace text path

What stays outside:

- allocator ownership
- `MemoryBudgetSystem` ownership
- broad render-budget live retries
- bootstrap/audio sequencing ownership

### 3. CD Asset / Bootstrap

Status:

- Active consolidated

Primary active boundary doc:

- `CD_ASSET_ACTIVE_CONTRACTS_BOUNDARY_CONSOLIDATED.md`

Current live entry points:

- `src/main.cxx`
- `src/cd_asset_bootstrap_runtime_bridge.hpp`
- `src/memory_budget_runtime_bridge.hpp`

Current live surfaces:

- `CdAssetSbaBootstrapDecisionPacket`
- `CdAssetAnchorBootstrapDecisionPacket`
- bootstrap-local SBA helper
- bootstrap-local anchor fallback helper
- `ShouldPreferCartForCdStaging()`

What stays outside:

- broad `CdAssetFramePacket` runtime use
- request/read/parse packet ownership in bootstrap
- asynchronous CD job behavior

### 4. Presenter

Status:

- Active consolidated at narrow seam, broader branch deferred

Primary active runtime seam:

- HUD status-line boundary through `PresenterBoundaryStatusTextPacket`

Current live entry points:

- `src/game_loop_system.hpp`
- presenter HUD/debug path

Current live surfaces:

- `DrivingHudTextPacket`
- `PresenterBoundaryStatusTextPacket`
- `PresentPresenterBoundaryHudStatusTextPacket(...)`

What stays outside:

- broader presenter decision/facade hierarchy
- larger runtime presenter retries
- broader presenter ownership migration

### 5. Scheduler / Reuse

Status:

- Active consolidated at narrow seam, broader branch frozen

Primary docs:

- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `SIMULATION_REUSE_LIVE_RETRY_BLOCKER.md`
- `SCHEDULER_REUSE_BRANCH_FINAL_STATUS.md`

Current live entry points:

- `src/game_loop_system.hpp`
- local reuse observability/debug assembly path

Current live surfaces:

- `SimulationSchedulerTelemetryViewPacket`
- `TrackRenderProducerStatePacket`
- track-only reuse observability seam

What stays outside:

- broader symmetric simulation-side retry
- larger live reuse aggregate above the current accepted seam

### 6. Car Render

Status:

- Active consolidated at narrow runtime seam, broader branch deferred

Primary docs:

- `CAR_RENDER_PASSIVE_FLOW_PLAN.md`
- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`
- `CAR_RENDER_BOUNDARY_CLOSURE_CONSOLIDATED.md`
- `CAR_RENDER_BRANCH_FINAL_STATUS.md`

Current live surfaces:

- `GameLoopRuntime::BuildCarRenderRuntimePacket(...)`
- `GameLoopRuntime::BuildCarShadowRuntimeDecision(...)`
- `Game::CarRenderSystem::RenderPacket`
- `GameLoopRuntime::BuildCarShadowPrepPacket(...)`
- `GameLoopRuntime::ApplyCarRenderRuntimeSync(...)`
- `GameLoopRuntime::SubmitCarRenderRuntime(...)`
- `Game::CarRenderSystem::ShadowPacket`
- `Game::CarRenderSystem::Telemetry`
- `DrawCarShadowBlob(...)`
- `DrawCarShadowModel(...)`
- no live dependency on `CarRenderFrameState`

What stays outside:

- broad `CarVisualFramePacket` runtime integration
- render submission ownership migration
- mesh-render ownership migration

### 7. AutoLap Route

Status:

- Active consolidated at narrow helper seam, broader branch frozen

Primary docs:

- `AUTO_LAP_ROUTE_PASSIVE_FLOW_PLAN.md`
- `AUTO_LAP_ROUTE_REINTRODUCTION_STRATEGY.md`
- `AUTO_LAP_BOUNDARY_CLOSURE_CONSOLIDATED.md`
- `AUTO_LAP_BRANCH_FINAL_STATUS.md`

Current live surfaces:

- `src/auto_lap_route_lifecycle_ops.hpp`
- `src/auto_lap_route_build_ops.hpp`
- `AutoLapGuideRouteTrace`

What stays outside:

- final runtime route ownership
- route-step mutation ownership
- broader `AutoLapFramePacket` live integration

## What is already closed well enough

The branch now has explicit active-boundary closure for:

- `track-render`
- `memory budget`
- `bootstrap/CD`
- the narrow final `presenter` HUD seam with broader branch defer
- the narrow final `scheduler/reuse` seam with broader branch freeze
- the narrow final `car render` runtime seam with broader branch defer
- the narrow final `AutoLap` helper seam with broader branch freeze

This means those subsystems now have:

- a named active boundary
- explicit host-owned entry points
- explicit live packet/helper surfaces
- explicit statement of what remains outside the boundary

## What is not yet fully closed

At the active-boundary inventory level, no additional subsystem remains open in
this branch.

## Recommended sequencing from here

Best next order:

1. keep the already closed active boundaries stable
2. avoid reopening trivial wrapper cleanup in those closed areas
3. only reopen a subsystem when there is a new explicit runtime goal
4. otherwise restrict follow-up work to documentation, validation, or targeted
   remove-first retries with fresh budget evidence

Final remaining-work list:

- `REFACTOR_FINAL_REMAINING_WORK.md`
