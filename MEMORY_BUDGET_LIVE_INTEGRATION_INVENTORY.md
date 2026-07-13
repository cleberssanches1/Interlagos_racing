# Memory Budget Live Integration Inventory

## Objective

Document the exact live integration status of the memory-budget chain and the
only acceptable future runtime substitution shapes.

This document is runtime-facing inventory only.

It does not authorize a live patch by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- allocator timing must remain unchanged
- critical runtime ownership remains in:
  - `src/game_loop_system.hpp`
  - `src/main.cxx`
  - `src/car_audio_system.hpp`

## Current live status

The memory-budget chain is partially live through:

- narrow bridge-level category-policy accessors
- local presentation-only consumption of `MemoryDebugPresentationBundle`

No broad allocator/policy packet is consumed live in critical runtime files:

- `GameLoopRuntime::MemoryBudgetFramePacket`
- `MemoryBudgetDomain::MemorySnapshotPacket`
- `MemoryBudgetDomain::MemoryPressurePacket`
- `MemoryBudgetDomain::MemoryBudgetPolicyPacket`
- `MemoryBudgetDomain::CategoryBudgetPolicyPacket`

Current live ownership remains local at the call sites.

The bridge only supplies narrow category-policy queries.

The consolidated runtime-bridge boundary is documented in:

- `MEMORY_BUDGET_RUNTIME_BRIDGE_BOUNDARY_CONSOLIDATED.md`

The presentation-side bundle consumers remain local to
`src/game_loop_system.hpp`.

Presentation formatting may be extracted into passive helpers if runtime call
sites and ordering remain owned by `src/game_loop_system.hpp`.

## Current live boundaries

### Boundary A - PCM setup

Live file:

- `src/car_audio_system.hpp`

Live accessor:

- `MemoryBudgetRuntimeBridge::ConfigurePcmStreamingBudgetFromPolicy()`

Current live timing:

- called during `CarAudioSystem::Initialize()`

Current ownership kept local:

- PCM setup timing
- audio-system initialization order
- sample loading order

### Boundary B - CD staging bootstrap hint

Live file:

- `src/main.cxx`

Live accessor:

- `MemoryBudgetRuntimeBridge::ShouldPreferCartForCdStaging()`

Current live timing:

- evaluated during bootstrap/setup flow in `main`

Current ownership kept local:

- bootstrap sequencing
- staging fallback behavior
- cart/high-work runtime ownership

### Boundary C - optional HUD telemetry gate

Live file:

- `src/game_loop_system.hpp`

Live accessor:

- `MemoryBudgetRuntimeBridge::ShouldAvoidHudOptionalTelemetry()`

Current live timing:

- evaluated inside `PresentFrameHudAndTelemetry(...)`

Current ownership kept local:

- HUD submission ownership
- periodic stats call order
- frame presentation ownership

### Boundary D - optional debug transient telemetry gate

Live file:

- `src/game_loop_system.hpp`

Live accessor:

- `MemoryBudgetRuntimeBridge::ShouldAvoidDebugTransientOptionalTelemetry()`

Current live timing:

- evaluated inside `PresentFrameHudAndTelemetry(...)`

Current ownership kept local:

- debug print ownership
- telemetry print ordering
- frame-end presentation ownership

### Boundary E - frame-end memory debug presentation bundle

Live file:

- `src/game_loop_system.hpp`

Live accessor:

- `BuildFrameEndMemoryDebugPresentationBundle()`
- `PresentMemoryDebugPresentationBundle(...)`

Current live timing:

- assembled and consumed inside `UpdateFrameEndOverlays()`

Current ownership kept local:

- frame-end debug update ownership
- work-RAM usage print ordering
- high/low trace print ordering
- memory-debug presentation call site

### Boundary F - low-work overlay memory debug presentation bundle

Live file:

- `src/game_loop_system.hpp`

Live accessor:

- `BuildLowWorkOverlayMemoryDebugPresentationBundle(...)`
- `PresentLowWorkOverlayMemoryDebugPacket(...)`
- `PresentLowWorkOverlayCompactTextBundle(...)`
- `PresentLowWorkOverlayFullTextBundle(...)`
- `CaptureAndBuildLowWorkOverlayBaseTextBundle(...)`
- `PresentLowWorkOverlayTextBundleByMode(...)`
- `PresentCapturedLowWorkOverlayByMode(...)`

Current live timing:

- assembled and consumed locally inside `UpdateLowWorkFreeOverlayEnabled()`

Current ownership kept local:

- low-work overlay update ownership
- overlay print ordering
- allocator timing
- runtime call-site ownership

Current live text coverage:

- `WLWR`
- `HWT`
- `LWC`
- `LTX`
- `LFO`
- full `LTK`
- both `PB` paths

### Boundary G - render-budget overlay text chain

Live file:

- none yet

Prepared passive surfaces:

- `BuildRenderBudgetObservabilityViewPacket(...)`
- `BuildRenderBudgetPresentationViewPacket(...)`
- `BuildRenderBudgetOverlayTextViewPacket(...)`
- `PresentRenderBudgetOverlayTextViewPacket(...)`

Current live timing:

- none

Current ownership kept local:

- render scheduling
- allocator timing
- presenter/debug print ordering
- runtime call-site ownership

Current blocker:

- no existing host-local textual read family currently consumes equivalent
  render-budget state in a remove-first shape
- the boundary is prepared, but there is not yet one safe live substitution
  target in `src/game_loop_system.hpp` or its active presenter helpers

Current allowed status:

- compile-only only until a real substitutional consumer exists

## Current passive-enriched consumers

These consumers already receive memory category policy, but they remain passive
enrichment only:

- former `src/game_loop_track_render_packet_assembler.hpp` removed after smoke validation stopped depending on it
- `src/game_loop_car_visual_packet_assembler.hpp`
- `src/game_loop_render_budget_observability_view_assembler.hpp`
- `src/game_loop_render_budget_presentation_view_assembler.hpp`
- `src/game_loop_render_budget_overlay_text_view_assembler.hpp`
- `src/game_loop_render_budget_overlay_text_bridge_assembler.hpp`
- `src/game_loop_render_budget_overlay_text_presenter_ops.hpp`
- `src/game_loop_observability_state_assembler.hpp`

Meaning:

- `TrackRenderFramePacket` carries `TrackRender` budget policy
- `CarVisualFramePacket` carries `CarRender` budget policy
- observability/render-budget flows can summarize those policies
- `RenderBudgetObservabilityViewPacket` narrows those policies for future
  debug/presentation-only consumers
- `RenderBudgetPresentationViewPacket` narrows them again to presentation/debug
  boundary fields only
- `RenderBudgetOverlayTextViewPacket` narrows them one level further to
  printable/renderable overlay flags
- one sibling compile-only textual presenter boundary now already exists above
  that path
- that chain currently has no approved live consumer because no equivalent
  host-local textual read family exists yet
- no allocator ownership moved
- no render scheduling ownership moved

## Runtime boundaries already prepared

### Narrow boundary family - bridge/category access

Prepared surfaces:

1. `MemoryBudgetRuntimeBridge::QueryCategoryPolicy(...)`
2. `MemoryBudgetRuntimeBridge::PreferredPoolForCategory(...)`
3. `MemoryBudgetRuntimeBridge::ShouldAvoidOptionalAllocationsForCategory(...)`

Target type:

- narrow category-policy query only

Current allowed live use:

- one boundary at a time
- remove-first substitutions only

### Broad boundary family - passive frame aggregation

Prepared surfaces:

1. `MemorySnapshotPacket`
2. `MemoryPressurePacket`
3. `MemoryBudgetPolicyPacket`
4. `CategoryBudgetPolicyPacket`
5. `MemoryTelemetryPacket`
6. `MemoryBudgetFramePacket`

Target type:

- upstream/off-path aggregation only

Current allowed live use:

- none in critical runtime files

## Explicitly non-live layers

The following are prepared but must not be the first live consumer in critical
runtime files:

- `MemorySnapshotPacket`
- `MemoryPressurePacket`
- `MemoryBudgetPolicyPacket`
- `CategoryBudgetPolicyPacket`
- `MemoryTelemetryPacket`
- `MemoryBudgetFramePacket`

These remain useful upstream/off-path, but they are too broad for the first
consumer on a critical runtime boundary.

## Remove-first rule

Any future live memory-budget patch must:

1. target one boundary only
2. consume only the narrowest bridge/category surface prepared for that boundary
3. remove equivalent local gating or pool-choice logic in the same patch
4. keep allocator timing unchanged
5. avoid mixing memory live integration with unrelated scheduler/render/bootstrap
   changes

## Current prohibited live moves

Do not do these in the first live retry:

- consume `MemoryBudgetFramePacket` directly in `src/game_loop_system.hpp`
- consume `MemoryBudgetFramePacket` directly in `src/main.cxx`
- move `RunWorkRamMaintenance` ownership
- move allocator trim/floor logic into a new host helper
- replace multiple memory boundaries in one patch
- mix memory live integration with loop-critical refactors

## Acceptance criteria for a future live retry

Every future memory-budget live patch must keep:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- no allocator timing drift
- no HUD/debug ordering drift
- no bootstrap sequencing drift

## Validation ritual

Required after every future live attempt:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Related documents

- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_CATEGORY_CONSUMER_MATRIX.md`
- `MEMORY_BUDGET_RENDER_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTER_DEBUG_BOUNDARY_PLAN.md`
- `MEMORY_BUDGET_RUNTIME_BRIDGE_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_FRAME_END_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `MEMORY_BUDGET_PASSIVE_FLOW_PLAN.md`
- `MEMORY_REINTRODUCTION_STRATEGY.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
