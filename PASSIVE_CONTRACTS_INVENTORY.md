# Passive Contracts Inventory

## Objective

Provide one consolidated index of the passive-contract groundwork already in the
repo, so future runtime cuts can be chosen without re-reading every domain
plan first.

This document is inventory-only.

It does not authorize broader runtime substitutions by itself.

## Stable baseline

- reference ISO: `4134912`
- emulator boot must remain stable
- critical runtime files still require substitution-first discipline:
  - `src/game_loop_system.hpp`
  - `src/main.cxx`

## Domains

### Presenter / Observability

Primary plans:

- `PRESENTER_PASSIVE_REFACTOR_INVENTORY.md`
- `GAME_LOOP_OBSERVABILITY_FLOW_PLAN.md`
- `GAME_LOOP_PRESENTER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PRESENTER_FACADE_CHAIN_FLOW_PLAN.md`
- `PRESENTER_FACADE_LIVE_INTEGRATION_INVENTORY.md`

Main passive families:

- presentation bundles
- render debug bundles
- overlay/debug bundles
- observability debug bundles
- presenter facade request/decision/bridge packets
- frame-end and HUD/telemetry decision packets
- presenter observability input adapter
- presenter facade input adapter
- presenter facade decision input adapter
- presenter facade decision bridge
- presenter HUD/telemetry decision input adapter

Representative files:

- `src/game_loop_presentation_debug_contracts.hpp`
- `src/game_loop_render_debug_contracts.hpp`
- `src/game_loop_overlay_debug_contracts.hpp`
- `src/game_loop_observability_contracts.hpp`
- `src/game_loop_presenter_facade_contracts.hpp`
- `src/game_loop_presenter_facade_interface_contracts.hpp`
- `src/game_loop_presenter_frame_end_decision_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_input_contracts.hpp`
- `src/game_loop_presenter_observability_input_contracts.hpp`
- `src/game_loop_presenter_facade_input_contracts.hpp`
- `src/game_loop_presenter_facade_decision_input_contracts.hpp`
- `src/game_loop_presenter_facade_decision_bridge_contracts.hpp`

Runtime status:

- mostly compile-only
- prior live presenter attempts were rolled back
- presenter runtime ownership remains in `src/game_loop_system.hpp`

### Track Render

Primary plans:

- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `TRACK_RENDER_REINTRODUCTION_STRATEGY.md`

Main passive families:

- full frame packet
- reduced debug packet
- telemetry view packet
- producer-state packet
- producer-hint packet

Representative files:

- `src/game_loop_track_render_packet.hpp`
- `src/game_loop_track_render_debug_contracts.hpp`
- `src/game_loop_track_render_telemetry_view_contracts.hpp`
- `src/game_loop_track_render_producer_state_contracts.hpp`
- `src/game_loop_track_render_producer_hint_contracts.hpp`

Runtime status:

- minimal live substitution is active for:
  - overlay query metrics
  - SH2 telemetry snapshot path
  - producer in-flight hint path
- these cuts are intentionally narrow and stable

### Reuse / Scheduler groundwork

Primary plans:

- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PASSIVE_TO_RUNTIME_INTEGRATION_PLAN.md`
- `SIMULATION_SCHEDULER_PLAN.md`
- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`
- `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`

Main passive families:

- track reuse decision view
- track reuse telemetry view
- simulation reuse decision view
- simulation reuse telemetry view
- reuse observability aggregate
- simulation drain view
- simulation completion view
- simulation scheduler telemetry view
- simulation scheduler lifecycle observability aggregate
- scheduler/reuse observability aggregate
- scheduler/reuse flow observability aggregate
- scheduler/reuse debug telemetry packet

Representative files:

- `src/game_loop_track_reuse_decision_view_contracts.hpp`
- `src/game_loop_track_reuse_telemetry_view_contracts.hpp`
- `src/game_loop_simulation_reuse_decision_view_contracts.hpp`
- `src/game_loop_simulation_reuse_telemetry_view_contracts.hpp`
- `src/game_loop_reuse_observability_contracts.hpp`
- `src/game_loop_simulation_drain_view_contracts.hpp`
- `src/game_loop_simulation_completion_view_contracts.hpp`
- `src/game_loop_simulation_scheduler_telemetry_view_contracts.hpp`
- `src/game_loop_simulation_scheduler_lifecycle_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_flow_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_debug_telemetry_contracts.hpp`

Runtime status:

- compile-only
- prepared to keep future track/simulation reuse substitutions symmetric
- no live scheduler ownership change has been introduced here
- the preferred next narrow runtime-facing candidate is
  `SimulationSchedulerTelemetryViewPacket` in one local observability-only
  helper before any reuse aggregate is attempted

### CD Asset / Bootstrap

Primary plans:

- `CD_ASSET_PASSIVE_FLOW_PLAN.md`
- `CD_ASSET_MINIMAL_BOOTSTRAP_SUBSTITUTION_PLAN.md`
- `CD_REINTRODUCTION_STRATEGY.md`
- `CD_BOOTSTRAP_CHAIN_FLOW_PLAN.md`

Main passive families:

- full CD asset frame packet
- bootstrap decision packet
- SBA bootstrap decision packet
- anchor bootstrap decision packet
- bootstrap decision bridge packet

Representative files:

- `src/game_loop_cd_asset_packet.hpp`
- `src/game_loop_cd_asset_bootstrap_decision_contracts.hpp`
- `src/game_loop_cd_asset_sba_decision_contracts.hpp`
- `src/game_loop_cd_asset_anchor_decision_contracts.hpp`
- `src/game_loop_cd_asset_decision_bridge_contracts.hpp`

Runtime status:

- bootstrap flow remains runtime-owned by `src/main.cxx`
- prior live bootstrap retries were rolled back
- current cuts remain compile-only except previously accepted bridge-level neutral uses

### Memory Budget

Primary plans:

- `MEMORY_BUDGET_PASSIVE_FLOW_PLAN.md`
- `MEMORY_REINTRODUCTION_STRATEGY.md`
- `MEMORY_BUDGET_CHAIN_FLOW_PLAN.md`
- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `MEMORY_BUDGET_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `MEMORY_BUDGET_CATEGORY_CONSUMER_MATRIX.md`
- `MEMORY_BUDGET_RENDER_OBSERVABILITY_FLOW_PLAN.md`
- `MEMORY_BUDGET_PRESENTER_DEBUG_BOUNDARY_PLAN.md`
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

Main passive families:

- memory-budget frame packet
- runtime bridge category-policy surface
- category-budget policy packet
- render-budget observability flow
- render-budget observability view
- render-budget presentation/debug view
- render-budget overlay/text view
- memory presentation / trace / debug bundles
- memory-debug presentation boundary inventory
- low-work overlay text view
- high-work trace text view
- low-work trace text view

Representative files:

- `src/game_loop_memory_budget_packet.hpp`
- `src/memory_budget_contracts.hpp`
- `src/memory_budget_runtime_bridge.hpp`
- `src/game_loop_render_budget_observability_view_contracts.hpp`
- `src/game_loop_render_budget_presentation_view_contracts.hpp`
- `src/game_loop_render_budget_overlay_text_view_contracts.hpp`
- `src/game_loop_memory_presentation_contracts.hpp`
- `src/game_loop_memory_overlay_text_view_contracts.hpp`
- `src/game_loop_memory_trace_text_view_contracts.hpp`
- `src/game_loop_memory_trace_text_low_work_view_contracts.hpp`
- `src/game_loop_memory_debug_contracts.hpp`
- `src/game_loop_observability_contracts.hpp`

Runtime status:

- selected neutral bridges are already live
- allocator timing and ownership remain intentionally unchanged
- future live retries should prefer bridge/category boundaries over broad frame
  packet ownership in critical runtime files
- one local `MemoryDebugPresentationBundle` consumer is now active in the
  frame-end debug path without moving ownership out of `src/game_loop_system.hpp`
- one additional local `MemoryDebugPresentationBundle` consumer is now active
  in the low-work overlay path without moving ownership out of
  `src/game_loop_system.hpp`
- that low-work overlay consumer now covers `WLWR`, `HWT`, `LWC`, `LTX`,
  `LFO`, full `LTK`, and both `PB` paths through the same local bundle/text
  boundary

### AutoLap Route

Primary plans:

- `AUTO_LAP_ROUTE_PASSIVE_FLOW_PLAN.md`
- `AUTO_LAP_ROUTE_REINTRODUCTION_STRATEGY.md`

Main passive families:

- full AutoLap frame packet
- guide/load/build/step packet slices
- guide-route trace

Representative files:

- `src/game_loop_auto_lap_packet.hpp`
- `src/game_loop_auto_lap_packet_assembler.hpp`
- `src/auto_lap_route_contracts.hpp`

Runtime status:

- mostly passive
- prior additive runtime packet trial exceeded binary budget
- future runtime moves must be substitutional, not additive

## Current live-vs-passive summary

### Stable live narrow cuts

- `TrackRenderTelemetryViewPacket` consumer path
- `TrackRenderProducerStatePacket` intermediary path
- `TrackRenderProducerHintPacket` final in-flight hint path
- selected neutral `MemoryBudget` bridges

### Compile-only families

- presenter facade / frame-end / HUD decision layers
- CD bootstrap decision layers
- reuse decision/telemetry view layers
- reuse observability aggregate
- most AutoLap packetization

## Validation hooks

Compile-only SH2 validation:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

Stable-build validation:

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Selection rule for the next runtime cut

Prefer the next cut only if all are true:

- it consumes an already existing passive packet
- it removes or replaces equivalent local logic in the same patch
- it does not widen ownership in a critical file
- it keeps ISO at `4134912`

## Recommended next low-risk directions

1. continue documentation-first consolidation before new live cuts
2. prefer substitutional reuse/scheduler observability over broad scheduler moves
3. start with `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md` Boundary A
   (`SimulationSchedulerTelemetryViewPacket`) before touching reuse aggregates
4. keep bootstrap/presenter changes compile-only unless a remove-first patch is obvious
