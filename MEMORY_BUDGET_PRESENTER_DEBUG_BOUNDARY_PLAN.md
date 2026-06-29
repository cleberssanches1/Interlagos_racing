# Memory Budget Presenter / Debug Boundary Plan

## Objective

Document how the `Memory Budget` render-budget packets should fit into future
presenter/debug boundaries without widening runtime ownership.

This document is boundary-only.

It does not authorize live presenter/debug integration.

Companion consolidated boundary:

- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_CONSOLIDATED.md`

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- presenter/debug runtime ownership must remain unchanged
- render scheduling ownership must remain unchanged

## Current passive narrowing

The render-budget path now has these passive layers:

1. `TrackRenderFramePacket`
2. `CarVisualFramePacket`
3. `RenderBudgetPacketFlow`
4. `RenderBudgetObservabilityViewPacket`
5. `RenderBudgetPresentationViewPacket`
6. `RenderBudgetOverlayTextViewPacket`

## Boundary roles

### `RenderBudgetObservabilityViewPacket`

Purpose:

- observability/debug-oriented narrowing
- retains per-consumer pressure and optional-allocation facts
- still suitable for off-path debug aggregation

Not ideal as first presentation/debug boundary because:

- it still exposes two full consumer summaries
- it is broader than a text/overlay path needs

### `RenderBudgetPresentationViewPacket`

Purpose:

- presentation/debug-facing summary
- keeps only the fields expected by a future presenter/debug decision path

Good candidate for:

- one passive presentation adapter
- one presenter-side summary packet

### `RenderBudgetOverlayTextViewPacket`

Purpose:

- final text/overlay-facing narrowing
- keeps only printable/renderable budget flags and preferred pools

Good candidate for:

- one debug text helper
- one overlay summary call
- one presenter/debug-only read boundary

## Preferred future boundary chains

### Future observability/debug summary boundary

Preferred chain:

1. `TrackRenderFramePacket`
2. `CarVisualFramePacket`
3. `RenderBudgetPacketFlow`
4. `RenderBudgetObservabilityViewPacket`

Use when:

- the consumer still needs explicit track/car split facts
- the boundary is not yet text/overlay-only

### Future presenter/debug summary boundary

Preferred chain:

1. `TrackRenderFramePacket`
2. `CarVisualFramePacket`
3. `RenderBudgetPacketFlow`
4. `RenderBudgetObservabilityViewPacket`
5. `RenderBudgetPresentationViewPacket`

Use when:

- the consumer is presenter-adjacent
- the boundary wants summary fields only
- the consumer should not walk `CategoryBudgetPolicy`

### Future text/overlay boundary

Preferred chain:

1. `TrackRenderFramePacket`
2. `CarVisualFramePacket`
3. `RenderBudgetPacketFlow`
4. `RenderBudgetObservabilityViewPacket`
5. `RenderBudgetPresentationViewPacket`
6. `RenderBudgetOverlayTextViewPacket`

Use when:

- the consumer is purely textual/overlay
- only printable flags and pool summaries are required

## Remove-first rule

Any future live retry must:

1. choose only one boundary family
2. consume only the narrowest packet required by that boundary
3. remove equivalent local reads in the same patch
4. keep presenter/debug print ownership unchanged

## Current non-goals

Do not do these in the first live retry:

- consume `RenderBudgetPacketFlow` directly in a presenter/debug host boundary
- mix render-budget presenter integration with scheduler/audio/bootstrap changes
- move print/overlay ownership together with packet substitution

## Related documents

- `MEMORY_BUDGET_RENDER_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_CATEGORY_CONSUMER_MATRIX.md`
- `PRESENTER_FACADE_CHAIN_FLOW_PLAN.md`
