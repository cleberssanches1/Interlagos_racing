# Memory Budget Memory Debug Presentation Minimal Live Substitution Plan

## Objective

Define the smallest acceptable future live substitution order for the
`Memory Budget` memory-debug presentation side, using only the already prepared
passive text/presentation packets and preserving emulator stability.

This plan exists because these paths are presentation-only, but they still sit
inside critical frame-end/debug ownership in `src/game_loop_system.hpp`.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- no invalid opcode
- no silent close

## Runtime boundaries covered

Only these boundary types are in scope:

- low-work overlay text/update paths
- high-work trace print path
- low-work trace print path
- unified memory-debug presentation handoff

Not in scope for the first retry:

- allocator ownership changes
- `MemoryPresentationPacketFlow` as a direct live boundary
- multi-boundary substitution in one patch
- scheduler/audio/bootstrap changes in the same patch

## Narrow packets to use

### Overlay text boundary

Use only:

- `LowWorkOverlayTextViewPacket`
- `LowWorkOverlayTextBundle`

Current text surface:

- header summary
- high-work summary
- low-work breakdown
- low-work ticks
- tag-group summary
- allocator summary

### Trace text boundaries

Use only:

- `HighWorkTraceTextViewPacket`
- `HighWorkTraceTextPacket`
- `LowWorkTraceTextViewPacket`
- `LowWorkTraceTextPacket`

Current text surface:

- high-work delta counters
- low-work stage/frame deltas

### Unified presentation boundary

Use only:

- `MemoryDebugPresentationBundle`

Current summary surface:

- `MemoryPresentationPacketFlow`
- `LowWorkOverlayTextBundle`
- `HighWorkTraceTextPacket`
- `LowWorkTraceTextPacket`

## Required substitution order

### Step 1 - low-work overlay text boundary first

The first acceptable live retry should target only one low-work overlay text
path.

Patch shape:

1. assemble `LowWorkOverlayTextViewPacket` locally
2. consume it only in one overlay/update helper family
3. replace only equivalent local text field reads
4. keep packet assembly stack-local

Must remain unchanged:

- overlay print/update ownership
- update ordering
- allocator timing
- all non-memory debug paths

### Step 2 - high-work trace boundary second

Only after repeated stable runs from Step 1:

1. assemble `HighWorkTraceTextViewPacket` locally
2. consume it only in one high-work trace print/log helper
3. replace only equivalent local trace delta reads
4. keep logging ownership local

Must remain unchanged:

- trace capture timing
- log ordering
- high-work maintenance ownership

### Step 3 - low-work trace boundary third

Only after repeated stable runs from Steps 1 and 2:

1. assemble `LowWorkTraceTextViewPacket` locally
2. consume it only in one low-work trace print/log helper
3. replace only equivalent local trace delta reads
4. keep logging ownership local

Must remain unchanged:

- trace capture timing
- log ordering
- low-work maintenance ownership

### Step 4 - unified memory-debug presentation boundary last

Only after the narrower subpaths have each been proven stable independently:

1. assemble `MemoryDebugPresentationBundle` locally
2. consume it only in one presentation/debug-only adapter or sibling helper
3. remove equivalent lower-level read composition in the same patch
4. keep it stack-local and substitutional

Must remain unchanged:

- print ownership
- debug update ordering
- allocator timing
- frame pacing

## What must not be pulled into the first live boundary

Do not pull these directly into the first live retry:

- `MemorySnapshotPacket`
- `MemoryPresentationPacketFlow`
- `WorkRamUsagePacket`
- `LowWorkOverlayPacket`
- `HighWorkTracePacket`
- `LowWorkTracePacket`

Those structures may remain upstream/off-path, but the first live boundary
should consume only the already narrowed text/presentation packets.

## Remove-first rule

Each live patch must be substitutional.

That means:

- if a packet field replaces a local text/debug read, the original read must be
  removed in the same patch
- if no equivalent reads are removed, the packet must remain passive-only

## Acceptance criteria

Every future live patch in this sequence must keep:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- no allocator timing drift
- no debug text ordering drift
- no frame-end overlay ordering drift

## Validation ritual

Required after every live attempt:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Abort conditions

Rollback immediately if:

- ISO grows above `4134912`
- emulator no longer boots
- invalid opcode appears
- a boundary needs more than its narrow packet
- ownership starts moving together with the text/presentation substitution
- multiple memory-debug subpaths start moving in the same patch

## Related documents

- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_BOUNDARY_INVENTORY.md`
- `GAME_LOOP_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PASSIVE_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
