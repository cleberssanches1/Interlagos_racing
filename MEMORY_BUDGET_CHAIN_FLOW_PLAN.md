# Memory Budget Chain Flow Plan

## Objective

Consolidate the current passive memory-budget chain into one document that
shows the narrowing order, intended future live boundaries, and current
runtime-neutral status.

This document is inventory-and-flow only.

It does not authorize allocator/runtime ownership changes.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- memory runtime ownership remains distributed across existing runtime code

## Chain overview

The current memory-budget chain is intentionally layered from broad memory
capture to narrow category-policy/runtime bridge decisions:

1. snapshot capture layer
2. pressure classification layer
3. policy classification layer
4. category-policy layer
5. telemetry layer
6. frame packet layer
7. runtime bridge layer
8. downstream passive consumer layer

## Layer-by-layer narrowing

### 1. Snapshot capture layer

Files:

- `src/memory_budget_system.hpp`
- `src/memory_budget_contracts.hpp`
- `src/memory_budget_policy_assembler.hpp`
- `src/memory_budget_transition_ops.hpp`

Primary packets:

- `Game::MemoryBudgetSystem::Snapshot`
- `MemoryBudgetDomain::MemorySnapshotPacket`

Purpose:

- preserve one explicit memory snapshot with free-space/block metadata
- keep raw capture separated from later policy interpretation

### 2. Pressure classification layer

Files:

- `src/memory_budget_contracts.hpp`
- `src/memory_budget_policy_assembler.hpp`
- `src/memory_budget_transition_ops.hpp`

Primary packets:

- `MemoryBudgetDomain::Thresholds`
- `MemoryBudgetDomain::MemoryPressurePacket`

Purpose:

- classify high-work and low-work pressure without touching allocator behavior
- keep threshold-driven interpretation passive and explicit

### 3. Policy classification layer

Files:

- `src/memory_budget_contracts.hpp`
- `src/memory_budget_policy_assembler.hpp`
- `src/memory_budget_transition_ops.hpp`

Primary packet:

- `MemoryBudgetDomain::MemoryBudgetPolicyPacket`

Purpose:

- derive global memory policy decisions from snapshot and pressure
- expose PCM/cart/optional-allocation guidance without changing live ownership

### 4. Category-policy layer

Files:

- `src/memory_budget_contracts.hpp`
- `src/memory_budget_category_assembler.hpp`
- `src/memory_budget_transition_ops.hpp`

Primary packets:

- `MemoryBudgetDomain::CategoryBudgetPolicy`
- `MemoryBudgetDomain::CategoryBudgetPolicyPacket`

Purpose:

- narrow global memory policy into per-consumer decisions
- make category intent explicit for:
  - `TrackRender`
  - `CarRender`
  - `AudioPcm`
  - `Hud`
  - `CdStaging`
  - `DebugTransient`

### 5. Telemetry layer

Files:

- `src/memory_budget_contracts.hpp`
- `src/memory_budget_telemetry_assembler.hpp`
- `src/memory_budget_transition_ops.hpp`

Primary packet:

- `MemoryBudgetDomain::MemoryTelemetryPacket`

Purpose:

- expose condensed free-space/pressure telemetry for overlays and diagnostics
- keep presentation/debug consumers away from deeper capture/policy logic

### 6. Frame packet layer

Files:

- former passive memory-budget frame packet pair removed after smoke validation
  stopped depending on it

Primary packet:

- `GameLoopRuntime::MemoryBudgetFramePacket`

Purpose:

- aggregate the passive memory state into one frame-local bundle
- preserve a single packet above later visual/debug/render consumers

### 7. Runtime bridge layer

Files:

- `src/memory_budget_runtime_bridge.hpp`

Primary surface:

- `Game::MemoryBudgetRuntimeBridge`

Current accessors:

- `ConfigurePcmStreamingBudgetFromPolicy()`
- `ShouldPreferCartForCdStaging()`
- `ShouldAvoidDebugTransientOptionalTelemetry()`
- `ShouldAvoidHudOptionalTelemetry()`
- `QueryCategoryPolicy(...)`
- `PreferredPoolForCategory(...)`
- `ShouldAvoidOptionalAllocationsForCategory(...)`

Purpose:

- expose narrow runtime-neutral category-policy queries
- keep current runtime call sites from assembling the full passive chain inline

### 8. Downstream passive consumer layer

Representative files:

- `src/game_loop_track_render_packet_assembler.hpp`
- `src/game_loop_car_visual_packet_assembler.hpp`
- `src/game_loop_render_budget_observability_view_contracts.hpp`
- `src/game_loop_render_budget_observability_view_assembler.hpp`
- `src/game_loop_render_budget_presentation_view_contracts.hpp`
- `src/game_loop_render_budget_presentation_view_assembler.hpp`
- `src/game_loop_render_budget_overlay_text_view_contracts.hpp`
- `src/game_loop_render_budget_overlay_text_view_assembler.hpp`
- former `src/game_loop_memory_presentation_packet_assembler.hpp` removed after
  its helpers were absorbed by the live memory debug assembler
- `src/game_loop_memory_debug_packet_assembler.hpp`
- `src/game_loop_observability_state_assembler.hpp`

Representative packets:

- `TrackRenderFramePacket` budget fields
- `CarVisualFramePacket` budget fields
- `MemoryPresentationPacketFlow`
- `MemoryDebugPresentationBundle`
- `RenderBudgetPacketFlow`
- `RenderBudgetObservabilityViewPacket`
- `RenderBudgetPresentationViewPacket`
- `RenderBudgetOverlayTextViewPacket`

Purpose:

- carry memory policy into observability and future render-budget consumers
- keep these consumers passive until a remove-first live substitution is chosen

### Future render-budget observability boundary

Preferred passive chain:

1. `TrackRenderFramePacket`
2. `CarVisualFramePacket`
3. `RenderBudgetPacketFlow`
4. `RenderBudgetObservabilityViewPacket`
5. `RenderBudgetPresentationViewPacket`
6. `RenderBudgetOverlayTextViewPacket`

## Boundary-focused summary

### Future PCM bootstrap/runtime boundary

Preferred passive chain:

1. `MemorySnapshotPacket`
2. `MemoryPressurePacket`
3. `MemoryBudgetPolicyPacket`
4. `CategoryBudgetPolicy`
5. `MemoryBudgetRuntimeBridge::ConfigurePcmStreamingBudgetFromPolicy()`

### Future CD staging boundary

Preferred passive chain:

1. `MemorySnapshotPacket`
2. `MemoryPressurePacket`
3. `MemoryBudgetPolicyPacket`
4. `CategoryBudgetPolicy`
5. `MemoryBudgetRuntimeBridge::ShouldPreferCartForCdStaging()`

### Future optional HUD/debug observability boundary

Preferred passive chain:

1. `MemorySnapshotPacket`
2. `MemoryPressurePacket`
3. `MemoryBudgetPolicyPacket`
4. `CategoryBudgetPolicy`
5. `MemoryBudgetRuntimeBridge::ShouldAvoidOptionalAllocationsForCategory(...)`

## Current runtime status

- passive memory packetization is complete from snapshot to frame packet
- selected neutral bridge accessors are already live
- allocator timing, pool ownership, and maintenance order remain unchanged
- `src/game_loop_system.hpp` and `src/main.cxx` still own their critical runtime
  sequencing

Companion consolidated bridge boundary:

- `MEMORY_BUDGET_RUNTIME_BRIDGE_BOUNDARY_CONSOLIDATED.md`

## Remove-first rule for future live retries

Any future live retry must:

1. target one consumer boundary only
2. consume only the narrowest bridge/packet already prepared for that boundary
3. remove equivalent local policy gating in the same patch
4. keep allocator timing unchanged
5. avoid broad packet ownership in critical runtime files

## Validation

Compile-only SH2 validation:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

Stable build validation:

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Related documents

- `MEMORY_BUDGET_PASSIVE_FLOW_PLAN.md`
- `MEMORY_BUDGET_CATEGORY_CONSUMER_MATRIX.md`
- `MEMORY_BUDGET_RENDER_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTER_DEBUG_BOUNDARY_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_REINTRODUCTION_STRATEGY.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
