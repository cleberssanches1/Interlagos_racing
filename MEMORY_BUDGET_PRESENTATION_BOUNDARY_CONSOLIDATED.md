# Memory Budget Presentation Boundary Consolidated

## Objective

Record the current consolidated state of the render-budget presentation-facing
boundary inside the `Memory Budget` passive chain.

This document is inventory-only.

It does not authorize runtime ownership changes by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- render scheduling ownership must remain unchanged
- presenter/debug ownership must remain unchanged

## Consolidated boundary

The render-budget presentation boundary is now explicitly split into these
passive layers:

### 1. Category-policy capture

Files:

- `src/memory_budget_runtime_bridge.hpp`

Role:

- expose per-category policy through narrow bridge queries
- keep memory policy separate from runtime presenter ownership

### 2. Render packet enrichment

Files:

- `src/game_loop_track_render_packet_assembler.hpp`
- `src/game_loop_car_visual_packet_assembler.hpp`

Role:

- attach `TrackRender` and `CarRender` memory-budget policy to the existing
  passive render packets

### 3. Render-budget flow aggregation

Files:

- `src/game_loop_observability_state_assembler.hpp`
- `src/game_loop_observability_contracts.hpp`

Role:

- group track/car render budget slices into `RenderBudgetPacketFlow`

### 4. Observability view narrowing

Files:

- `src/game_loop_render_budget_observability_view_contracts.hpp`
- `src/game_loop_render_budget_observability_view_assembler.hpp`

Role:

- reduce the broad flow to explicit observability facts
- preserve track/car split and per-consumer pressure data

### 5. Presentation/debug view narrowing

Files:

- `src/game_loop_render_budget_presentation_view_contracts.hpp`
- `src/game_loop_render_budget_presentation_view_assembler.hpp`

Role:

- reduce the observability view to presenter/debug-facing fields only

### 6. Overlay/text narrowing

Files:

- `src/game_loop_render_budget_overlay_text_view_contracts.hpp`
- `src/game_loop_render_budget_overlay_text_view_assembler.hpp`

Role:

- reduce the presentation/debug view to printable/renderable flags only

## Current boundary selection order

Use the narrowest boundary that matches the consumer:

1. `RenderBudgetOverlayTextViewPacket`
2. `RenderBudgetPresentationViewPacket`
3. `RenderBudgetObservabilityViewPacket`

## What still stays outside this boundary

The following ownership remains outside this passive chain:

- track render scheduling
- car render scheduling
- producer/submission ownership
- presenter runtime ownership
- overlay print ownership
- allocator timing decisions

## Why this boundary is considered consolidated

It now has:

- explicit layer-by-layer narrowing
- explicit packet selection guidance
- clear separation between policy capture and presentation consumption
- no runtime ownership migration
- no render scheduling migration

## Best future uses

This consolidated boundary is best suited for:

- compile-only presenter/debug preparation
- off-path observability summaries
- future remove-first textual overlay retries
- future narrow presenter-adjacent summary boundaries

## Do next

Preferred next moves:

1. keep this boundary compile-only until a remove-first runtime target is
   obvious
2. prefer `RenderBudgetOverlayTextViewPacket` for the first textual retry
3. prefer `RenderBudgetPresentationViewPacket` for a non-text presenter summary
   retry
4. keep `RenderBudgetPacketFlow` out of critical host boundaries

## Do not do next

- consume `RenderBudgetPacketFlow` directly in a first live presenter/debug
  retry
- move render scheduling behind these packets
- mix this boundary with scheduler/audio/bootstrap changes
- move overlay/print ownership together with packet substitution

## Related documents

- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_RENDER_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTER_DEBUG_BOUNDARY_PLAN.md`
- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
