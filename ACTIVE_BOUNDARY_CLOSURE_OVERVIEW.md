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

- Active consolidated, but narrow

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

- Passive/documental

Primary docs:

- `CAR_RENDER_PASSIVE_FLOW_PLAN.md`
- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`
- `CAR_RENDER_BOUNDARY_CLOSURE_CONSOLIDATED.md`

Current posture:

- broad runtime integration still deferred by binary budget

### 7. AutoLap Route

Status:

- Passive/documental

Primary docs:

- `AUTO_LAP_ROUTE_PASSIVE_FLOW_PLAN.md`
- `AUTO_LAP_ROUTE_REINTRODUCTION_STRATEGY.md`
- `AUTO_LAP_BOUNDARY_CLOSURE_CONSOLIDATED.md`

Current posture:

- route helpers and packet slices exist
- runtime ownership remains mostly local and deferred

## What is already closed well enough

The branch now has explicit active-boundary closure for:

- `track-render`
- `memory budget`
- `bootstrap/CD`
- the narrow active `presenter` HUD seam
- the narrow final `scheduler/reuse` seam with broader branch freeze

This means those subsystems now have:

- a named active boundary
- explicit host-owned entry points
- explicit live packet/helper surfaces
- explicit statement of what remains outside the boundary

## What is not yet fully closed

The branch does not yet have the same level of active-boundary closure for:

- `car render`
- `AutoLap`

For those subsystems, the branch currently relies more on:

- passive flow documents
- blocker documents
- minimal retry plans

## Recommended sequencing from here

Best next order:

1. keep the already closed active boundaries stable
2. avoid reopening trivial wrapper cleanup in those closed areas
3. pick one remaining non-closed subsystem at a time:
   - `car render`
   - or `AutoLap`
4. only after that, consider a higher-level closure pass for the remaining
   passive/documental areas

Final remaining-work list:

- `REFACTOR_FINAL_REMAINING_WORK.md`
