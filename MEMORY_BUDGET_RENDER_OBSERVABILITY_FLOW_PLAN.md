# Memory Budget Render Observability Flow Plan

## Objective

Document the passive flow and ownership boundaries for render-budget data from
category policy to observability/presentation consumers.

This document is flow-only.

It does not authorize runtime ownership changes.

Companion consolidated boundary:

- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_CONSOLIDATED.md`

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- render scheduling ownership must remain unchanged
- allocator timing must remain unchanged

## Current passive flow

The render-budget slice is now intentionally layered:

1. category-policy capture
2. render packet enrichment
3. render-budget flow aggregation
4. render-budget observability view
5. render-budget presentation/debug view
6. render-budget overlay text view

## Layer-by-layer flow

### 1. Category-policy capture

Files:

- `src/memory_budget_runtime_bridge.hpp`

Primary surface:

- `MemoryBudgetRuntimeBridge::QueryCategoryPolicy(...)`

Purpose:

- expose per-category memory policy without widening runtime ownership

### 2. Render packet enrichment

Files:

- `src/game_loop_track_render_packet_assembler.hpp`
- `src/game_loop_car_visual_packet_assembler.hpp`

Primary packets:

- `TrackRenderFramePacket`
- `CarVisualFramePacket`

Purpose:

- attach `TrackRender` and `CarRender` budget policy to already existing
  passive render packets

### 3. Render-budget flow aggregation

Files:

- `src/game_loop_observability_contracts.hpp`
- `src/game_loop_observability_state_assembler.hpp`

Primary packet:

- `RenderBudgetPacketFlow`

Purpose:

- group the track/car render budget slices in one observability-facing flow

### 4. Render-budget observability view

Files:

- `src/game_loop_render_budget_observability_view_contracts.hpp`
- `src/game_loop_render_budget_observability_view_assembler.hpp`

Primary packets:

- `RenderBudgetConsumerViewPacket`
- `RenderBudgetObservabilityViewPacket`

Purpose:

- reduce `RenderBudgetPacketFlow` to the exact per-consumer budget facts needed
  by future observability/debug users
- avoid carrying `CategoryBudgetPolicy` into presentation/debug boundaries

### 5. Render-budget presentation/debug view

Files:

- `src/game_loop_render_budget_presentation_view_contracts.hpp`
- `src/game_loop_render_budget_presentation_view_assembler.hpp`

Primary packet:

- `RenderBudgetPresentationViewPacket`

Purpose:

- reduce the observability view to presentation/debug-facing fields only
- keep a future HUD/debug boundary from consuming the broader render-budget flow

### 6. Render-budget overlay text view

Files:

- `src/game_loop_render_budget_overlay_text_view_contracts.hpp`
- `src/game_loop_render_budget_overlay_text_view_assembler.hpp`

Primary packet:

- `RenderBudgetOverlayTextViewPacket`

Purpose:

- reduce the presentation/debug view to printable/renderable flags only
- keep a future text/overlay boundary from consuming broader presentation state

## Ownership summary

Current ownership stays split as follows:

- category policy assembly:
  - `MemoryBudgetRuntimeBridge`
- render packet ownership:
  - track/car packet assemblers
- render-budget flow ownership:
  - observability assembly layer
- presentation/debug summary ownership:
  - compile-only passive layer only

No live render scheduling, producer, or submission ownership moved in this
slice.

## Best future boundary

The preferred future presentation/debug chain is:

1. `TrackRenderFramePacket`
2. `CarVisualFramePacket`
3. `RenderBudgetPacketFlow`
4. `RenderBudgetObservabilityViewPacket`
5. `RenderBudgetPresentationViewPacket`
6. `RenderBudgetOverlayTextViewPacket`

The first future consumer should be:

- one debug/presentation-only read path
- one stack-local assembly point
- no render scheduling side effects

## Remove-first rule

Any future live retry must:

1. consume only the narrowest packet required by the target boundary
2. remove equivalent local reads in the same patch
3. keep render scheduling ownership unchanged
4. keep allocator timing unchanged

## Current non-goals

Do not do these in the first live retry:

- consume `RenderBudgetPacketFlow` directly in a critical host boundary
- move track/car render scheduling decisions behind the packet
- mix this boundary with bootstrap/scheduler/audio changes

## Validation

Compile-only SH2 validation:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

## Related documents

- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_CATEGORY_CONSUMER_MATRIX.md`
- `MEMORY_BUDGET_PRESENTER_DEBUG_BOUNDARY_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
