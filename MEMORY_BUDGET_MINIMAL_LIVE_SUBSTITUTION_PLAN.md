# Memory Budget Minimal Live Substitution Plan

## Objective

Define the smallest acceptable future live substitution order for
`Memory Budget`, using only already accepted bridge/category boundaries.

This plan exists because memory policy is close to allocator timing, bootstrap
sequencing, HUD/debug gating, and PCM setup timing.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- no invalid opcode
- no silent close

## Runtime boundaries covered

Only these already prepared boundary types are in scope:

- PCM setup policy selection
- CD staging pool preference
- optional HUD telemetry gating
- optional debug transient telemetry gating

Not in scope for the first retry:

- allocator ownership changes
- `RunWorkRamMaintenance` migration
- broad `MemoryBudgetFramePacket` integration in critical runtime files
- multi-boundary substitution in one patch

## Narrow surfaces to use

Use only bridge/category-level surfaces:

- `MemoryBudgetRuntimeBridge::ConfigurePcmStreamingBudgetFromPolicy()`
- `MemoryBudgetRuntimeBridge::ShouldPreferCartForCdStaging()`
- `MemoryBudgetRuntimeBridge::ShouldAvoidHudOptionalTelemetry()`
- `MemoryBudgetRuntimeBridge::ShouldAvoidDebugTransientOptionalTelemetry()`
- `MemoryBudgetRuntimeBridge::QueryCategoryPolicy(...)`
- `MemoryBudgetRuntimeBridge::PreferredPoolForCategory(...)`
- `MemoryBudgetRuntimeBridge::ShouldAvoidOptionalAllocationsForCategory(...)`

Current decision surface:

- preferred pool by category
- optional-allocation avoidance by category
- runtime-neutral policy queries only

## Required substitution order

### Step 1 - HUD/debug optional telemetry gates first

The first acceptable live retry should stay inside
`PresentFrameHudAndTelemetry(...)`.

Patch shape:

1. use only:
   - `ShouldAvoidHudOptionalTelemetry()`
   - `ShouldAvoidDebugTransientOptionalTelemetry()`
2. replace only equivalent local optional-telemetry gating
3. keep packet/policy assembly behind the bridge
4. keep the call order inside `PresentFrameHudAndTelemetry(...)` unchanged

Must remain unchanged:

- HUD submission ownership
- debug print ownership
- periodic stats ordering
- frame-end presentation ordering

### Step 2 - CD staging boundary second

Only after repeated stable runs from Step 1:

1. use only `ShouldPreferCartForCdStaging()`
2. replace only equivalent local pool-preference logic
3. keep bootstrap sequencing unchanged
4. keep staging fallback ownership local to `src/main.cxx`

Must remain unchanged:

- bootstrap order
- model/bootstrap setup ownership
- cart/high-work fallback sequencing

### Step 3 - PCM setup boundary third

Only after repeated stable runs from Steps 1 and 2:

1. use only `ConfigurePcmStreamingBudgetFromPolicy()`
2. replace only equivalent local PCM allocation-policy selection
3. keep audio initialization order unchanged
4. keep sample/cue loading ownership local to `CarAudioSystem`

Must remain unchanged:

- PCM initialization timing
- engine/shift/tire sample loading order
- audio runtime ownership

### Step 4 - generic category query consumers last

Only after the explicit boundary helpers have been proven stable:

1. use only:
   - `QueryCategoryPolicy(...)`
   - `PreferredPoolForCategory(...)`
   - `ShouldAvoidOptionalAllocationsForCategory(...)`
2. target one passive-enriched consumer family at a time
3. keep the consumer local and substitutional

Best candidate shapes:

- one render-budget observability assembly point
- one passive packet enrichment point
- one non-critical debug/presentation query point

Preferred passive narrowing for that family:

1. `TrackRenderFramePacket`
2. `CarVisualFramePacket`
3. `RenderBudgetPacketFlow`
4. `RenderBudgetObservabilityViewPacket`
5. `RenderBudgetPresentationViewPacket`
6. `RenderBudgetOverlayTextViewPacket`

Must remain unchanged:

- render scheduling ownership
- track/car render submission order
- allocator timing

## What must not be pulled into the first live boundary

Do not pull these directly into the first live retry:

- `MemorySnapshotPacket`
- `MemoryPressurePacket`
- `MemoryBudgetPolicyPacket`
- `CategoryBudgetPolicyPacket`
- `MemoryTelemetryPacket`
- `MemoryBudgetFramePacket`

Those structures may remain upstream/off-path, but the first live boundary
should consume only the already accepted narrow bridge/category surfaces.

## Remove-first rule

Each live patch must be substitutional.

That means:

- if a bridge/category query replaces a local gate or pool-choice read, the
  original logic must be removed in the same patch
- if no equivalent logic is removed, the integration should remain passive or
  bridge-neutral only

## Acceptance criteria

Every future live patch in this sequence must keep:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- no allocator timing drift
- no HUD/debug ordering drift
- no bootstrap sequencing drift
- no PCM init-order drift

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
- the boundary needs more than one narrow bridge/category surface
- allocator timing starts moving together with the substitution
- bootstrap/audio/HUD ownership starts moving with the policy substitution

## Related documents

- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_CATEGORY_CONSUMER_MATRIX.md`
- `MEMORY_BUDGET_RENDER_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTER_DEBUG_BOUNDARY_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `MEMORY_BUDGET_PASSIVE_FLOW_PLAN.md`
- `MEMORY_REINTRODUCTION_STRATEGY.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
