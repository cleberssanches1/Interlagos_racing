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

Companion consolidated bridge boundary:

- `MEMORY_BUDGET_RUNTIME_BRIDGE_BOUNDARY_CONSOLIDATED.md`

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

The passive chain is now also consolidated in:

- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `MEMORY_BUDGET_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `MEMORY_BUDGET_CATEGORY_CONSUMER_MATRIX.md`
- `MEMORY_BUDGET_RENDER_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTER_DEBUG_BOUNDARY_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

That chain document should be treated as the primary broad-to-narrow inventory
for future live retries.

The live integration inventory should be treated as the boundary-by-boundary
status map for already accepted neutral bridge uses.

Another safe passive step now available for this slice is:

- explicit `Thresholds` builder in `src/memory_budget_transition_ops.hpp`
- explicit single-category builder in `src/memory_budget_transition_ops.hpp`
- explicit `MemoryBudgetFramePacket` builder in
  `src/game_loop_memory_budget_packet_assembler.hpp`

This means the passive memory-budget side now has a complete
snapshot-to-frame-packet path ready for future substitutional runtime cuts.

## Applied safe runtime bridge

A first substitutional runtime cut is now in place for PCM bootstrap setup:

- `src/memory_budget_runtime_bridge.hpp`
- `src/car_audio_system.hpp`

Current behavior remains intentionally equivalent:

- capture current memory snapshot
- build passive thresholds with zero pressure floors
- derive `AudioPcm` preferred pool from passive policy/category-policy
- map the resulting pool to `SRL::Sound::Pcm::SetMemAllocationBehaviour(...)`

This keeps:

- original PCM setup timing
- original high-work preferred block rule (`192 KiB`)
- original cart fallback behavior
- old `MemoryBudgetSystem` interface intact for compatibility

Another safe runtime cut now in place for category-policy consumption is:

- `src/memory_budget_runtime_bridge.hpp`
- `src/game_loop_system.hpp`

Current behavior remains intentionally neutral under the current thresholds:

- optional frame debug telemetry now queries `DebugTransient` policy explicitly
- no allocator ownership changed
- no gameplay/render/audio path changed
- with the current zero-pressure bridge thresholds, the telemetry path stays enabled

This still improves responsibility placement because optional observability no longer
depends on a host-local assumption about transient budget policy.

An additional safe runtime cut is now in place for `Hud` optional telemetry:

- `src/memory_budget_runtime_bridge.hpp`
- `src/game_loop_system.hpp`

Current behavior remains intentionally neutral under the current thresholds:

- periodic HUD telemetry now queries `Hud` policy explicitly
- the driving HUD and critical frame presentation remain untouched
- with the current zero-pressure bridge thresholds, the periodic HUD telemetry path stays enabled

The bridge is now also consolidated around explicit category accessors:

- `QueryCategoryPolicy(...)`
- `PreferredPoolForCategory(...)`
- `ShouldAvoidOptionalAllocationsForCategory(...)`

This keeps behavior unchanged while reducing duplicated category-specific helpers
before any future migration of more sensitive consumers.

A further passive render-side cut is now in place:

- `src/game_loop_track_render_packet.hpp`
- `src/game_loop_track_render_packet_assembler.hpp`
- `src/game_loop_car_visual_packet.hpp`
- `src/game_loop_car_visual_packet_assembler.hpp`

Current effect:

- passive visual packets now carry category-policy snapshots for `TrackRender` and `CarRender`
- no scheduler, draw submission or runtime render behavior changed
- this prepares future render-budget consumers without touching the critical render path

Those passive render-budget packets are now also consumable from observability contracts:

- `src/game_loop_observability_contracts.hpp`
- `src/game_loop_observability_state_assembler.hpp`

Current effect:

- observability can assemble passive render-budget flow from track/car visual packets
- no frame-loop consumer was switched to this flow yet
- this creates the internal documentation/contract layer before any runtime usage

That observability slice now also exposes complete passive builders:

- `BuildRenderBudgetPolicyPacket(...)`
- `BuildRenderBudgetPacketFlow(...)`
- `BuildFrameObservabilityPacket(...)`

This means a full observability packet can now be assembled off the critical path
with render-budget information included, without changing any live frame orchestration.

That off-path assembly is now also centralized in a dedicated helper:

- `src/game_loop_observability_packet_assembler.hpp`

Current effect:

- a full `FrameObservabilityPacket` can be assembled directly from overlay, telemetry,
  render and memory packet inputs
- this remains a local/passive utility and is still not connected to the live loop

That same off-path observability slice now has dedicated compile-only SH2 validation:

- `tools/validate_game_loop_observability_headers.ps1`

This reduces risk for future render-budget and observability refactors without
touching the critical runtime path.

The memory/debug presentation side now also has a dedicated off-path packet assembler:

- `src/game_loop_memory_presentation_packet_assembler.hpp`

Current effect:

- raw memory snapshot, low-work overlay inputs, and trace state can be assembled into
  passive memory-presentation packets outside `src/game_loop_system.hpp`
- an observability-ready `MemoryPresentationPacketFlow` can be built without adding any
  runtime ownership or frame-loop changes

The trace side now also exposes derived delta packets off-path:

- `src/game_loop_memory_trace_packet_assembler.hpp`

Current effect:

- high-work trace deltas (`alloc/free/realloc/failed`, blocks, finish/sync accumulation)
  can now be built from `HighWorkTracePacket`
- low-work stage deltas (`GLW1`-`GLW5` style free/payload/overhead deltas and track-draw
  sub-deltas) can now be built from `LowWorkTracePacket`
- this prepares future formatting extraction without changing any live debug print path

The next passive layer is now also prepared for textual trace extraction:

- `src/game_loop_memory_trace_text_contracts.hpp`
- `src/game_loop_memory_trace_text_assembler.hpp`

Current effect:

- the trace-only print payloads for `GH3`/`GH4` and `GLW1`-`GLW5` can now be assembled
  outside `src/game_loop_system.hpp`
- runtime print calls still remain untouched
- future formatting extraction can now substitute line-by-line from passive text packets

The low-work overlay textual layer is now also prepared off-path:

- `src/game_loop_memory_overlay_text_contracts.hpp`
- `src/game_loop_memory_overlay_text_assembler.hpp`

Current effect:

- `WLWR`, `HWT`, `LWC`, `LTK`, `LTX`, and `LFO` print payloads now have passive text packets
- a full low-work overlay text bundle can now be assembled outside `src/game_loop_system.hpp`
- runtime overlay rendering remains untouched

The memory/debug side now also has one consolidated passive assembly point:

- `src/game_loop_memory_debug_contracts.hpp`
- `src/game_loop_memory_debug_packet_assembler.hpp`

Current effect:

- one off-path bundle can now carry:
  - observability-ready `MemoryPresentationPacketFlow`
  - low-work overlay text bundle
  - high-work trace text packet
  - low-work trace text packet
- future extraction can move from scattered builders to one passive assembly call
- runtime ownership and print timing remain untouched
- one local frame-end consumer is now active in `src/game_loop_system.hpp`
- one local low-work overlay consumer is now active in `src/game_loop_system.hpp`
- that low-work overlay consumer now drives `WLWR`, `HWT`, `LWC`, `LTX`,
  `LFO`, full `LTK`, and both `PB` paths through the local bundle/text boundary

That consolidated memory/debug slice is now also joinable with frame observability
through one off-path bundle:

- `src/game_loop_observability_debug_contracts.hpp`
- `src/game_loop_observability_debug_packet_assembler.hpp`

Current effect:

- one passive `ObservabilityDebugBundle` can now carry both:
  - `FrameObservabilityPacket`
  - `MemoryDebugPresentationBundle`
- future overlay/debug presenter extraction can move to a single assembled input
- runtime frame execution remains untouched

The non-memory overlay/debug side now also has an off-path bundle:

- `src/game_loop_overlay_debug_text_contracts.hpp`
- `src/game_loop_overlay_debug_text_assembler.hpp`
- `src/game_loop_overlay_debug_contracts.hpp`
- `src/game_loop_overlay_debug_packet_assembler.hpp`

Current effect:

- spatial, shadow, face/shadow, ground-probe, physics-query, and segment-event
  debug payloads can now be assembled as passive text packets
- one `OverlayDebugBundle` can now carry both overlay/telemetry flows and their
  text-ready debug payloads
- runtime overlay printing remains untouched

The presentation/HUD side now also has a passive bundle:

- `src/game_loop_presentation_debug_contracts.hpp`
- `src/game_loop_presentation_debug_assembler.hpp`

Current effect:

- frame presentation, driving HUD text, periodic HUD stats, and realtime FPS packet
  can now be grouped outside `src/game_loop_system.hpp`
- future presenter extraction can reuse a single passive presentation bundle
- runtime HUD/update order remains untouched

That presenter-facing passive groundwork is now also consolidated one level higher:

- `src/game_loop_presenter_input_contracts.hpp`
- `src/game_loop_presenter_input_assembler.hpp`
- `src/game_loop_presenter_render_debug_contracts.hpp`
- `src/game_loop_presenter_render_debug_assembler.hpp`
- `src/game_loop_presenter_overlay_debug_contracts.hpp`
- `src/game_loop_presenter_overlay_debug_assembler.hpp`
- `src/game_loop_presenter_input_summary_contracts.hpp`
- `src/game_loop_presenter_input_summary_assembler.hpp`
- `src/game_loop_presenter_facade_contracts.hpp`
- `src/game_loop_presenter_facade_assembler.hpp`
- `src/game_loop_presenter_facade_interface_contracts.hpp`
- `src/game_loop_presenter_facade_interface_assembler.hpp`
- `GAME_LOOP_PRESENTER_FACADE_PLAN.md`

Current effect:

- memory/debug presentation can now participate in one top-level presenter input
  alongside presentation/HUD and overlay/debug bundles
- presenter-side render/debug consumption can also use a narrower summary packet
  instead of walking the full render aggregate
- presenter-side overlay/observability consumption can also use a narrower
  summary packet instead of walking the full overlay + observability aggregates
- presenter-side top-level flow control can now use one input summary packet
  before touching the deeper passive presenter bundles
- presenter-side facade handoff can now use one directly consumable facade packet
  instead of rebuilding HUD/render/overlay slices at the call site
- presenter-side integration order is now explicitly documented through a facade
  request/decision contract before any live substitution is attempted
- the full passive presenter inventory is now centralized in
  `PRESENTER_PASSIVE_REFACTOR_INVENTORY.md`
- this reduces future host-side stitching before any runtime migration of the
  presenter path
- allocator, audio, render and frame-loop behavior remain untouched
