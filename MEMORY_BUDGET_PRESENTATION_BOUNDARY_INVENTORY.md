# Memory Budget Presentation Boundary Inventory

## Objective

Provide one consolidated inventory of the `Memory Budget` presentation-facing
boundaries already prepared in the passive chain, so future presenter/debug
retries can choose the narrowest safe packet without re-reading multiple plans.

This document is inventory-only.

It does not authorize runtime integration by itself.

The consolidated boundary summary now lives in:

- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_CONSOLIDATED.md`

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- presenter/debug runtime ownership must remain unchanged

## Current presentation-facing hierarchy

The current `Memory Budget` presentation-facing path is intentionally layered:

1. `TrackRenderFramePacket`
2. `CarVisualFramePacket`
3. `RenderBudgetPacketFlow`
4. `RenderBudgetObservabilityViewPacket`
5. `RenderBudgetPresentationViewPacket`
6. `RenderBudgetOverlayTextViewPacket`

## Boundary inventory

### Layer 1 - render packet boundary

Packets:

- `TrackRenderFramePacket`
- `CarVisualFramePacket`

Role:

- broad render-local packet enrichment with per-category budget policy

Use status:

- passive only

Do not use first for:

- presenter/debug retry
- overlay/text retry

### Layer 2 - observability flow boundary

Packet:

- `RenderBudgetPacketFlow`

Role:

- aggregate track/car render budget into one observability-facing flow

Use status:

- passive only

Do not use first for:

- presenter/debug host boundary
- overlay/text boundary

### Layer 3 - observability view boundary

Packet:

- `RenderBudgetObservabilityViewPacket`

Role:

- preserve explicit track/car split
- preserve per-consumer pressure and optional-allocation facts

Best fit:

- debug aggregation
- observability-only summary
- off-path trace/analysis consumers

### Layer 4 - presentation/debug summary boundary

Packet:

- `RenderBudgetPresentationViewPacket`

Role:

- summary packet for presenter-adjacent consumers
- removes need to walk full consumer summaries

Best fit:

- presenter/debug summary adapter
- one future HUD/debug decision path
- non-text presentation summary

### Layer 5 - overlay/text boundary

Packet:

- `RenderBudgetOverlayTextViewPacket`

Role:

- narrowest printable/renderable summary
- exposes only:
  - show/hide flags
  - aggregate pressure/guard flags
  - preferred pools

Best fit:

- debug text helper
- overlay summary call
- presenter/debug textual boundary

## Which boundary to choose

### If the consumer still needs explicit track vs car separation

Use:

- `RenderBudgetObservabilityViewPacket`

### If the consumer is presenter-adjacent but not purely textual

Use:

- `RenderBudgetPresentationViewPacket`

### If the consumer is purely textual/overlay-oriented

Use:

- `RenderBudgetOverlayTextViewPacket`

## First-live retry rule

Any future live retry must choose only one of these entry points:

1. `RenderBudgetObservabilityViewPacket`
2. `RenderBudgetPresentationViewPacket`
3. `RenderBudgetOverlayTextViewPacket`

And must also:

- consume the narrowest packet required by the target boundary
- remove equivalent local reads in the same patch
- keep print ownership unchanged
- keep render scheduling ownership unchanged

## Explicit non-goals

Do not do these in the first live retry:

- consume `RenderBudgetPacketFlow` directly in a presenter/debug host boundary
- consume `TrackRenderFramePacket` directly in a text/overlay boundary
- combine render-budget presentation substitution with scheduler/audio/bootstrap
  changes
- move overlay/print ownership with the packet substitution

## Recommended selection order

1. `RenderBudgetOverlayTextViewPacket`
2. `RenderBudgetPresentationViewPacket`
3. `RenderBudgetObservabilityViewPacket`

Rationale:

- start with the narrowest textual boundary first
- only widen if the consumer truly needs richer structure

## Related documents

- `MEMORY_BUDGET_RENDER_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTER_DEBUG_BOUNDARY_PLAN.md`
- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_CATEGORY_CONSUMER_MATRIX.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
