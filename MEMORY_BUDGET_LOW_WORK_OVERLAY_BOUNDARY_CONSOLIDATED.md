# Memory Budget Low Work Overlay Boundary Consolidated

## Objective

Record the current consolidated state of the `LowWorkOverlay` passive boundary
inside the `Memory Budget` presentation path.

This document is inventory-only.

It does not authorize runtime ownership changes by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- host sequencing must remain in `src/game_loop_system.hpp`

## Consolidated boundary

The `LowWorkOverlay` path is now split into three passive helper families:

### 1. Assembly helpers

Files:

- `src/game_loop_low_work_overlay_assembly_ops.hpp`

Role:

- build `LowWorkOverlayHeaderPacket`
- build the initial `LowWorkOverlayTextBundle`
- capture/build `HighWorkOverlayPacket`
- build `LowWorkOverlayBreakdownPacket`
- build `LowWorkOverlayTicksPacket`

### 2. Capture helpers

Files:

- `src/game_loop_low_work_overlay_capture_ops.hpp`

Role:

- capture `LWT` track-tag byte totals
- capture `LowWorkTagGroupPacket`
- derive `LowWorkTagGroupOverlay`
- capture `LowWorkAllocatorPacket`

### 3. Presentation helpers

Files:

- `src/game_loop_low_work_overlay_presenter_ops.hpp`

Role:

- present `WLWR`
- present `HWT`
- present `LWC`
- present compact/full `LTK` and `PB`
- present `LTX`
- present `LFO`

## What still stays in the host

The following ownership remains local to `src/game_loop_system.hpp`:

- cadence gating
- early return behavior
- overlay state writes
- `overlay.lastFreeBytes`
- `overlay.lastBreakdown`
- `overlay.lastTagGroup`
- `overlay.lastPayloadBytes`
- `overlay.lastOverheadBytes`
- `overlay.lastFreeBlocks`
- print/update ordering

## Functional coverage

The consolidated passive boundary now covers the full local low-work overlay
text path:

- `WLWR`
- `HWT`
- `LWC`
- `LWT`
- `LTX`
- `LFO`
- compact `LTK`
- full `LTK`
- both `PB` paths

## Why this boundary is considered consolidated

It now has:

- explicit capture helpers
- explicit packet assembly helpers
- explicit presentation helpers
- host-local ownership preserved
- no runtime ownership migration
- no allocator sequencing movement

## Recommended next moves

Do next:

1. keep this boundary runtime-stable
2. avoid broadening it into `MemoryPresentationPacketFlow`
3. use it only as precedent for other narrow presenter/debug boundaries
4. return to another compile-only boundary before any new live retry

Do not do next:

- move this whole overlay path off the host in one patch
- mix this boundary with scheduler/audio/bootstrap changes
- widen this path to broader memory/debug aggregates in a first live retry

## Related documents

- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
