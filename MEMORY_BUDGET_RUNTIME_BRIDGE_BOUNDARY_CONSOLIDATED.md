# Memory Budget Runtime Bridge Boundary Consolidated

## Objective

Record the current consolidated state of the narrow runtime-bridge boundary in
the `Memory Budget` chain.

This document is inventory-only.

It does not authorize runtime ownership changes by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- bootstrap sequencing must remain unchanged
- audio init order must remain unchanged
- HUD/debug call ordering must remain unchanged

## Consolidated boundary

The runtime-bridge side of `Memory Budget` is intentionally narrow and split by
consumer boundary.

### 1. Upstream passive policy chain

Files:

- `src/memory_budget_contracts.hpp`
- `src/memory_budget_policy_assembler.hpp`
- `src/memory_budget_category_assembler.hpp`
- `src/memory_budget_transition_ops.hpp`

Role:

- capture snapshot
- classify pressure
- derive global policy
- derive per-category policy

This layer stays upstream and off-path for critical runtime ownership.

### 2. Runtime bridge surface

Files:

- `src/memory_budget_runtime_bridge.hpp`

Role:

- expose narrow runtime-facing policy queries only
- keep critical runtime call sites from assembling broad packets inline

Current bridge accessors:

- `ConfigurePcmStreamingBudgetFromPolicy()`
- `ShouldPreferCartForCdStaging()`
- `ShouldAvoidHudOptionalTelemetry()`
- `ShouldAvoidDebugTransientOptionalTelemetry()`
- `QueryCategoryPolicy(...)`
- `PreferredPoolForCategory(...)`
- `ShouldAvoidOptionalAllocationsForCategory(...)`

## Current runtime boundaries

### Boundary A - PCM setup

Live file:

- `src/car_audio_system.hpp`

Bridge surface:

- `ConfigurePcmStreamingBudgetFromPolicy()`

Ownership still local:

- PCM initialization timing
- sample loading order
- audio runtime ownership

### Boundary B - CD staging bootstrap hint

Live file:

- `src/main.cxx`

Bridge surface:

- `ShouldPreferCartForCdStaging()`

Ownership still local:

- bootstrap sequencing
- staging fallback behavior
- cart/high-work ownership

### Boundary C - optional HUD telemetry gate

Live file:

- `src/game_loop_system.hpp`

Bridge surface:

- `ShouldAvoidHudOptionalTelemetry()`

Ownership still local:

- periodic HUD submission
- frame presentation ordering
- HUD runtime ownership

### Boundary D - optional debug transient telemetry gate

Live file:

- `src/game_loop_system.hpp`

Bridge surface:

- `ShouldAvoidDebugTransientOptionalTelemetry()`

Ownership still local:

- debug print ordering
- SH2 telemetry print ownership
- frame-end presentation ownership

## What is consolidated here

This boundary is now explicitly consolidated as:

- upstream policy assembly stays passive
- runtime bridge access stays narrow
- each critical consumer boundary uses one explicit bridge surface
- ownership remains local at each runtime call site

## What still stays outside this boundary

The following do not move into the bridge boundary:

- `MemoryBudgetFramePacket`
- direct frame-packet ownership in critical runtime files
- allocator maintenance ownership
- render scheduling ownership
- memory-debug presentation ownership

## Recommended future use

If a future live retry is needed, prefer this order:

1. HUD/debug optional telemetry gates
2. CD staging pool hint
3. PCM setup policy
4. generic category-query consumers

Use only one narrow bridge surface at a time and keep the patch
substitutional/remove-first.

## Do not do next

- widen this boundary to consume broad memory packets in critical runtime files
- combine multiple bridge substitutions in one patch
- mix this boundary with scheduler/audio/bootstrap structural changes
- move allocator timing or ownership into the bridge

## Related documents

- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `MEMORY_BUDGET_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_PASSIVE_FLOW_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
