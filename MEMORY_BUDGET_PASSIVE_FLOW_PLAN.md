# Memory Budget Passive Flow Plan

## Objective

Prepare the extraction of memory-budget policy decisions by category without
changing current allocation/runtime behavior.

## Current passive building blocks

- `src/memory_budget_contracts.hpp`
- `src/memory_budget_policy_assembler.hpp`
- `src/memory_budget_telemetry_assembler.hpp`
- `src/memory_budget_category_assembler.hpp`
- `src/memory_budget_transition_ops.hpp`
- `src/memory_budget_system.hpp`
- `src/game_loop_memory_budget_packet.hpp`
- `src/game_loop_memory_budget_packet_assembler.hpp`

These files already describe a passive memory-budget path for:

- snapshot assembly
- pressure assembly
- policy assembly
- telemetry assembly
- category-policy assembly
- frame-local aggregation of the memory-budget state

## Current runtime touch points

The memory-budget domain is currently consumed directly through:

1. snapshot capture for overlays/debug
2. PCM allocation policy setup
3. scattered category assumptions in bootstrap/runtime code

This means the domain already has passive packets, but still lacks one explicit
category-policy layer that answers:

- which pool each category prefers
- which categories should reduce pressure first
- which categories should avoid optional allocations

## Passive packet model

`src/game_loop_memory_budget_packet.hpp` aggregates:

- `MemorySnapshotPacket`
- `MemoryPressurePacket`
- `MemoryBudgetPolicyPacket`
- `CategoryBudgetPolicyPacket`
- `MemoryTelemetryPacket`

This stays:

- frame-local
- non-owning
- runtime-neutral until explicit integration is needed

## Runtime-to-passive substitution map

### Snapshot and pressure

Passive coverage already available:

1. `CaptureMemorySnapshotPacket(...)`
2. `BuildMemoryPressurePacket(...)`

### Global policy

Passive coverage already available:

1. `BuildMemoryPolicyPacket(...)`

Current outputs already modeled:

- PCM high-work preference
- PCM cart fallback preference
- streaming pressure reduction
- optional allocation avoidance

### Category policy

Passive coverage now available:

1. `BuildCategoryBudgetPolicyPacket(...)`
2. `SeedCategoryBudgetPolicyPacket(...)`
3. `SeedCategoryBudgetPolicy(...)`

Current category set:

- `TrackRender`
- `CarRender`
- `AudioPcm`
- `Hud`
- `CdStaging`
- `DebugTransient`

## Safe integration order

### Step 1 - keep allocation behavior unchanged

Do not change:

- current allocator call sites
- PCM runtime configuration timing
- cart/high-work fallback behavior

### Step 2 - packetize category policy only

When runtime integration becomes safe, assemble locally:

1. `MemorySnapshotPacket`
2. `MemoryPressurePacket`
3. `MemoryBudgetPolicyPacket`
4. `CategoryBudgetPolicyPacket`
5. `MemoryTelemetryPacket`
6. `MemoryBudgetFramePacket`

Consume immediately in the same scope.

No new persistent members.
No allocator ownership changes.
No boot/runtime ordering changes.

### Step 3 - centralize category consumers first

The best first runtime/bootstrap candidate is:

- expose one explicit category-policy packet to:
  - audio PCM setup
  - CD staging/bootstrap decisions
  - optional debug/transient allocations

This is lower risk than touching actual allocator internals.

### Step 4 - only later route all call sites through the policy packet

Only after repeated stable emulator runs:

- move per-category choices behind explicit packet consumers
- reduce scattered direct assumptions about pools
- then consider a true `MemoryBudgetSystem` facade integration

## Guard rails

- do not change allocator timing in the same patch
- do not mix this with track/car runtime cuts
- validate every step with `tools/validate_saturn_stable_build.ps1`
- keep ISO exactly `4134912`

## Immediate next safe step

The next safe step is:

1. keep runtime behavior unchanged
2. use this packet only as passive groundwork
3. later try a tiny cut that routes PCM/bootstrap decisions through the
   category-policy packet without changing allocator behavior
