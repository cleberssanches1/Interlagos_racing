# Memory Budget Render-Budget Overlay Text Minimal Live Substitution Plan

## Objective

Define the smallest acceptable future live substitution for the `RenderBudget`
textual boundary, using only the already prepared passive narrowing:

1. `RenderBudgetObservabilityViewPacket`
2. `RenderBudgetPresentationViewPacket`
3. `RenderBudgetOverlayTextViewPacket`

This plan is intentionally narrow.

It does not authorize broad render-budget integration in a critical host by
itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- render scheduling ownership must remain unchanged
- allocator timing must remain unchanged
- presenter/debug print ownership must remain unchanged

## Prepared passive boundary

The compile-only textual chain now exists as:

1. `src/game_loop_render_budget_overlay_text_bridge_assembler.hpp`
2. `src/game_loop_render_budget_overlay_text_presenter_ops.hpp`

The boundary can already:

- build `RenderBudgetObservabilityViewPacket`
- build `RenderBudgetPresentationViewPacket`
- build `RenderBudgetOverlayTextViewPacket`
- print one textual summary from the narrowest packet

## Allowed first live target

The first acceptable live target is:

- one debug/presenter-facing call path only
- one stack-local assembly point only
- one textual summary consumer only

Good shape:

1. local `TrackRenderFramePacket`
2. local `CarVisualFramePacket`
3. local `RenderBudgetOverlayTextViewPacket`
4. `PresentRenderBudgetOverlayTextViewPacket(...)`

## Best first host class

The first live target should not be:

- render scheduling
- render submission
- track/car runtime ownership
- frame-critical simulation orchestration

The best first live target is a non-critical debug/presenter-adjacent path
where:

- the host already owns textual output
- render packet state is already available locally
- no scheduling decision depends on the new packet

## Remove-first rule

The first live patch must be substitutional.

That means:

1. choose one exact local textual read family
2. replace only that family with `RenderBudgetOverlayTextViewPacket`
3. remove the equivalent local reads in the same patch
4. keep all print ownership local

If the patch only adds the packet and presenter without removing equivalent
reads, it should remain compile-only.

## What must not be used first

Do not use these first in a critical host retry:

- `RenderBudgetPacketFlow`
- direct `CategoryBudgetPolicy`
- direct `MemoryBudgetRuntimeBridge::QueryCategoryPolicy(...)` reads added to a
  new host branch
- any render scheduling branch
- any render submission branch

## Preferred future retry order

1. one textual debug helper outside the loop-critical hot path
2. one presenter-adjacent textual summary path
3. only later, if justified, a broader presentation summary using
   `RenderBudgetPresentationViewPacket`

## Current blocker

As of the current repo state, this boundary remains compile-only by design.

The prepared chain exists, but there is not yet one existing host-local textual
read family in `src/game_loop_system.hpp` or its current presentation helpers
that:

- already consumes the same render-budget semantics
- is debug/presenter-adjacent only
- can be removed and replaced in one patch

The existing render-budget path is therefore not blocked by missing packets.

It is blocked by the absence of a safe remove-first runtime consumer.

## Reopen condition

Reopen the first live retry only when one of these becomes true:

1. a real local presenter/debug path starts reading equivalent render-budget
   state directly
2. a narrow textual summary is introduced for another accepted reason and can
   be replaced immediately by `RenderBudgetOverlayTextViewPacket`
3. one non-critical helper acquires both local `TrackRenderFramePacket` and
   `CarVisualFramePacket` and already prints equivalent summary text

Until then, this boundary should remain documentation/passive-only.

## Acceptance criteria

Every future live patch for this boundary must keep:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- no render scheduling drift
- no allocator timing drift
- no debug ordering drift

## Validation ritual

Required after any future live attempt:

- `tools/validate_saturn_stable_build.ps1`
- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

## Related documents

- `MEMORY_BUDGET_RENDER_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTER_DEBUG_BOUNDARY_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
