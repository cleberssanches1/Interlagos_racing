# Memory Budget Frame End Boundary Consolidated

## Objective

Record the current consolidated state of the frame-end `Memory Budget`
presentation boundary.

This document is inventory-only.

It does not authorize runtime ownership changes by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- frame-end sequencing must remain in `src/game_loop_system.hpp`

## Consolidated boundary

The frame-end memory-debug boundary is now built from these passive families:

### 1. Memory debug bundle assembly

Files:

- `src/game_loop_memory_debug_packet_assembler.hpp`

Role:

- assemble `MemoryDebugPresentationBundle`
- group:
  - `MemoryPresentationPacketFlow`
  - `LowWorkOverlayTextBundle`
  - `HighWorkTraceTextPacket`
  - `LowWorkTraceTextPacket`

### 2. Memory debug presentation helpers

Files:

- `src/game_loop_memory_debug_presenter_ops.hpp`

Role:

- present work-RAM usage
- present high-work trace text
- present low-work trace text
- consume `MemoryDebugPresentationBundle` locally

### 3. Frame-end host-local assembly

Files:

- `src/game_loop_system.hpp`

Role:

- collect frame-end trace inputs
- collect track low-work deltas
- call `BuildFrameEndMemoryDebugPresentationBundle()`
- keep final call ordering local in `UpdateFrameEndOverlays()`

## What still stays in the host

The following ownership remains local to `src/game_loop_system.hpp`:

- frame-end cadence/order
- `UpdateFrameEndOverlays()` ownership
- `BuildFrameEndMemoryDebugPresentationBundle()` call site
- `PresentMemoryDebugPresentationBundle(...)` call site
- relationship with driving HUD/FPS/other frame-end helpers

## Functional coverage

The frame-end boundary now covers:

- work-RAM usage print path
- high-work trace print path
- low-work trace print path
- stack-local memory-debug bundle consumption

## Why this boundary is considered consolidated

It now has:

- explicit passive bundle assembly
- explicit passive presentation helpers
- host-local ownership preserved
- no runtime ownership migration
- no allocator sequencing movement

## Recommended next moves

Do next:

1. keep this boundary runtime-stable
2. avoid broadening it into a first live presenter retry
3. use it as precedent for other narrow memory/presenter boundaries

Do not do next:

- move the whole frame-end memory path off the host in one patch
- mix this boundary with scheduler/audio/bootstrap changes
- widen this boundary into broad memory policy ownership changes

## Related documents

- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
