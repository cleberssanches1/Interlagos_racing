# Memory Budget Memory Debug Presentation Boundary Inventory

## Objective

Provide one consolidated inventory of the `Memory Budget` memory-debug
presentation boundaries already prepared in the passive chain, so future
debug/overlay retries can choose the narrowest safe packet without re-reading
multiple plans.

This document is inventory-only.

It does not authorize runtime integration by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- debug/overlay runtime ownership must remain unchanged

## Current memory-debug presentation hierarchy

The current memory-debug presentation path is intentionally layered:

1. `MemorySnapshotPacket`
2. `MemoryPresentationPacketFlow`
3. `LowWorkOverlayTextBundle`
4. `LowWorkOverlayTextViewPacket`
5. `HighWorkTraceTextPacket`
6. `HighWorkTraceTextViewPacket`
7. `LowWorkTraceTextPacket`
8. `LowWorkTraceTextViewPacket`
9. `MemoryDebugPresentationBundle`

## Boundary inventory

### Layer 1 - snapshot / memory flow boundary

Packets:

- `MemorySnapshotPacket`
- `MemoryPresentationPacketFlow`

Role:

- broad memory/debug aggregation
- preserves work-RAM, overlay, and trace state in one flow

Use status:

- passive only

Do not use first for:

- text/overlay retry
- debug print retry

### Layer 2 - low-work overlay text boundary

Packet:

- `LowWorkOverlayTextBundle`

Role:

- text-oriented projection of the low-work overlay family
- groups:
  - header
  - high-work summary
  - low-work breakdown
  - low-work ticks
  - tag groups
  - allocator summary

Best fit:

- low-work overlay text helpers
- overlay-specific print/update paths

### Layer 3 - low-work overlay text view boundary

Packet:

- `LowWorkOverlayTextViewPacket`

Role:

- narrowest low-work overlay text summary
- keeps only printable/update-oriented flags and key counters

Best fit:

- one future low-work overlay text retry
- one stack-local overlay summary helper

### Layer 4 - trace text boundaries

Packets:

- `HighWorkTraceTextPacket`
- `LowWorkTraceTextPacket`

Role:

- text-oriented trace deltas only
- removes the broader trace snapshot structures from presentation boundaries

Best fit:

- trace print helpers
- debug/log-only trace paths

### Layer 5 - high-work trace text view boundary

Packet:

- `HighWorkTraceTextViewPacket`

Role:

- narrower high-work trace summary
- keeps only printable call-delta/block/sync flags and key counters

Best fit:

- one future high-work trace retry
- one stack-local high-work trace summary helper

### Layer 6 - low-work trace text view boundary

Packet:

- `LowWorkTraceTextViewPacket`

Role:

- narrower low-work trace summary
- keeps only printable stage/frame/allocator delta fields

Best fit:

- one future low-work trace retry
- one stack-local low-work trace summary helper

### Layer 7 - full memory debug presentation boundary

Packet:

- `MemoryDebugPresentationBundle`

Role:

- single passive bundle for memory-debug presentation
- groups:
  - `MemoryPresentationPacketFlow`
  - `LowWorkOverlayTextBundle`
  - `HighWorkTraceTextPacket`
  - `LowWorkTraceTextPacket`

Best fit:

- one future memory-debug presentation adapter
- one future presenter/debug-only summary consumer

Not ideal as first live retry because:

- it is broader than a single overlay or trace text boundary needs

## Which boundary to choose

### If the consumer is only updating low-work overlay text

Use:

- `LowWorkOverlayTextViewPacket`
- `LowWorkOverlayTextBundle`

### If the consumer is only logging one trace family

Use:

- `HighWorkTraceTextPacket`
- `HighWorkTraceTextViewPacket`
- `LowWorkTraceTextPacket`
- or `LowWorkTraceTextViewPacket`

### If the consumer needs a unified memory-debug presentation handoff

Use:

- `MemoryDebugPresentationBundle`

Current status:

- one local frame-end consumer is now active
- one local low-work overlay consumer is now active
- the low-work overlay consumer now covers `WLWR`, `HWT`, `LWC`, `LTX`,
  `LFO`, full `LTK`, and both `PB` paths through the local bundle/text path
- ownership still remains in `src/game_loop_system.hpp`
- print/update ordering remains local

## First-live retry rule

Any future live retry must choose only one of these entry points:

1. `LowWorkOverlayTextViewPacket`
2. `LowWorkOverlayTextBundle`
3. `HighWorkTraceTextViewPacket`
4. `HighWorkTraceTextPacket`
5. `LowWorkTraceTextViewPacket`
6. `LowWorkTraceTextPacket`
7. `MemoryDebugPresentationBundle`

And must also:

- consume the narrowest packet required by the target boundary
- remove equivalent local reads in the same patch
- keep print ownership unchanged
- keep allocator timing unchanged

## Explicit non-goals

Do not do these in the first live retry:

- consume `MemoryPresentationPacketFlow` directly in a text/overlay host boundary
- combine low-work overlay and trace retries in one patch
- move print/log ownership with the packet substitution
- mix memory-debug presentation substitution with scheduler/audio/bootstrap
  changes

## Recommended selection order

1. `LowWorkOverlayTextViewPacket`
2. `LowWorkOverlayTextBundle`
3. `HighWorkTraceTextViewPacket`
4. `HighWorkTraceTextPacket`
5. `LowWorkTraceTextViewPacket`
6. `LowWorkTraceTextPacket`
7. `MemoryDebugPresentationBundle`

Rationale:

- start with the narrowest overlay/text paths first
- only move to the full bundle after the subpaths are already proven stable

## Related documents

- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `GAME_LOOP_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PASSIVE_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
