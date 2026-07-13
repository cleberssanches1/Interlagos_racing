# Passive Contracts Inventory

## Objective

Provide one consolidated index of the passive-contract groundwork already in the
repo, so future runtime cuts can be chosen without re-reading every domain
plan first.

Operational next-cuts companion:

- `REFACTOR_RUNTIME_SAFE_NEXT_CUTS.md`
- `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`
- `GAME_LOOP_RUNTIME_CRITICAL_ENGINEERING_POLICY.md`

This document is inventory-only.

It does not authorize broader runtime substitutions by itself.

## Stable baseline

- reference ISO: `4134912`
- emulator boot must remain stable
- critical runtime files still require substitution-first discipline:
  - `src/game_loop_system.hpp`
  - `src/main.cxx`
- policy reference for critical-host size/growth decisions:
  - `GAME_LOOP_RUNTIME_CRITICAL_ENGINEERING_POLICY.md`

## Domains

### Presenter / Observability

Primary plans:

- `REFACTOR_CONSOLIDATED_FINAL_INDEX.md`
- `PRESENTER_PASSIVE_REFACTOR_INVENTORY.md`
- `GAME_LOOP_OBSERVABILITY_FLOW_PLAN.md`
- `GAME_LOOP_PRESENTER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PRESENTER_SUMMARY_SCHEDULER_REUSE_SIMULATION_BOUNDARY_CONSOLIDATED.md`
- `PRESENTER_SUMMARY_SCHEDULER_REUSE_SIMULATION_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PRESENTER_BOUNDARY_TEXT_FIRST_STATUS_RETRY_PATCH_PLAN.md`
- `PRESENTER_BOUNDARY_DECISION_RETRY_BLOCKER.md`
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
- presenter scheduler/reuse bridge adapter
- presenter scheduler/reuse preview
- presenter summary/scheduler-reuse preview
- presenter summary/scheduler-reuse simulation preview
- presenter summary/scheduler-reuse simulation view/text
- presenter summary/scheduler-reuse simulation text presenter
- presenter top boundary preview
- presenter top boundary view
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
- `src/game_loop_presenter_frame_end_preview_contracts.hpp`
- `src/game_loop_presenter_frame_end_preview_assembler.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_input_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_preview_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_preview_assembler.hpp`
- `src/game_loop_presenter_observability_input_contracts.hpp`
- `src/game_loop_presenter_scheduler_reuse_bridge_assembler.hpp`
- `src/game_loop_presenter_scheduler_reuse_preview_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_preview_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_preview_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_preview_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_view_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_view_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_simulation_text_presenter_ops.hpp`
- `src/game_loop_presenter_boundary_preview_contracts.hpp`
- `src/game_loop_presenter_boundary_view_contracts.hpp`
- `src/game_loop_presenter_facade_input_contracts.hpp`
- `src/game_loop_presenter_facade_decision_input_contracts.hpp`
- `src/game_loop_presenter_facade_decision_bridge_contracts.hpp`
- `src/game_loop_presenter_compile_only_preview_contracts.hpp`
- `src/game_loop_presenter_compile_only_preview_assembler.hpp`

Runtime status:

- mostly compile-only
- prior live presenter attempts were rolled back
- presenter runtime ownership remains in `src/game_loop_system.hpp`
- one narrow HUD status-line cut is already live through:
  - `DrivingHudTextPacket`
  - `PresenterBoundaryStatusTextPacket`
  - `PresentPresenterBoundaryHudStatusTextPacket(...)`
- one compile-only preview packet now validates the broad facade bridge plus
  the narrow decision bridge without touching runtime
- one additional compile-only HUD/telemetry preview now exists above
  `PresenterHudTelemetryDecisionPacket` for the `PresentFrameHudAndTelemetry(...)`
  boundary only
- one additional compile-only frame-end preview now exists above
  `PresenterFrameEndDecisionPacket` for the `UpdateFrameEndOverlays()` boundary
  only
- one higher compile-only presenter/resumo aggregate now also exists above the
  scheduler/reuse simulation preview path
- one sibling compile-only view/text path now also exists above that aggregate
- one sibling compile-only text presenter now also exists above that view/text

### Track Render

Primary plans:

- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `TRACK_RENDER_REINTRODUCTION_STRATEGY.md`
- `SCHEDULER_TRACK_RENDER_REUSE_FLOW_CONSOLIDATED.md`

Main passive families:

- full frame packet
- reduced debug packet
- telemetry view packet
- producer-state packet
- producer-hint packet
- presentation/observability aggregate packet
- SH2 presentation boundary packet

Representative files:

- `src/game_loop_track_render_packet.hpp`
- `src/game_loop_track_render_debug_contracts.hpp`
- `src/game_loop_track_render_telemetry_view_contracts.hpp`
- `src/game_loop_track_render_producer_state_contracts.hpp`
- `src/game_loop_track_render_producer_hint_contracts.hpp`
- `src/game_loop_track_render_presentation_observability_contracts.hpp`
- `src/game_loop_track_render_presentation_observability_presenter_ops.hpp`
- `src/game_loop_track_render_sh2_presentation_contracts.hpp`

Runtime status:

- minimal live substitution is active for:
  - overlay query metrics
  - SH2 telemetry snapshot path
  - producer in-flight hint path
- one local `TrackRenderSh2PresentationPacket` consumer is now also active in
  the same HUD/debug path
- one additional narrow local share is now active in the presentation/HUD path:
  - one `TrackRenderTelemetryViewPacket` can feed both:
    - `Sh2SplitTelemetrySnapshot`
    - local `TrackRenderSh2PresentationPacket`
- one compile-only preview now also exists directly above the current live
  track-render seam:
  - `TrackRenderTelemetryViewPacket`
  - `TrackRenderProducerHintPacket`
  - `TrackRenderSh2PresentationPacket`
- one compile-only preview now also exists directly above the scheduler
  lifecycle + track-render SH2 bridge:
  - `SimulationSchedulerLifecycleObservabilityPacket`
  - `TrackRenderTelemetryViewPacket`
  - `TrackRenderSh2PresentationPacket`
- one higher compile-only aggregate now exists above that live cut:
  - `TrackRenderPresentationObservabilityPacket`
- that packet remains compile-only and is not currently consumed by the runtime
  path
- one narrow live presentation line now already uses the higher local
  `TrackRenderSh2PresentationPacket` helper
- the `SH2 busy/sim` debug lines now also use external presenter helpers
- one compile-only packet now exists for the exact `PrintSh2SplitTelemetry(...)`
  boundary
- that exact boundary packet is not currently consumed by the runtime path
- these cuts are intentionally narrow and stable

### Car Render

Primary plans:

- `CAR_RENDER_PASSIVE_FLOW_PLAN.md`
- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`

Main passive families:

- frame context and render packet
- shadow packet assembly
- submit and telemetry packet assembly
- car visual frame packet
- shadow-prep preview packet

Representative files:

- `src/car_render_system.hpp`
- `src/car_shadow_assembler.hpp`
- `src/game_loop_car_shadow_prep_preview_contracts.hpp`
- `src/game_loop_car_visual_packet.hpp`

Runtime status:

- broad `CarVisualFramePacket` runtime integration remains deferred by binary budget
- the next safe runtime candidate remains the shadow-prep remove+replace slice
- one compile-only preview now exists directly above that slice:
  - `RenderPacket`
  - `ShadowPacket`
- submit ownership and draw entrypoints remain intentionally on the host

### Reuse / Scheduler groundwork

Primary plans:

- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PASSIVE_TO_RUNTIME_INTEGRATION_PLAN.md`
- `SIMULATION_SCHEDULER_PLAN.md`
- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`
- `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `SIMULATION_REUSE_RUNTIME_DEBUG_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SCHEDULER_REUSE_SIMULATION_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SCHEDULER_TRACK_RENDER_REUSE_FLOW_CONSOLIDATED.md`

Main passive families:

- track reuse decision view
- track reuse telemetry view
- track reuse observability aggregate
- track reuse preview
- track reuse preview presenter helper
- track reuse runtime bridge
- track reuse runtime preview presenter helper
- simulation reuse runtime commit helper
- simulation reuse runtime state helper
- simulation reuse runtime decision bridge
- simulation reuse runtime preview
- simulation reuse runtime preview presenter helper
- simulation reuse runtime preview presenter bridge
- simulation reuse runtime debug bridge helper
- simulation reuse runtime debug preview boundary
- simulation/track runtime seam assembler
- simulation reuse decision view
- simulation reuse telemetry view
- reuse observability aggregate
- reuse observability debug view
- reuse observability debug presenter helper
- reuse observability debug bundle
- simulation drain view
- simulation completion view
- simulation scheduler telemetry view
- simulation scheduler lifecycle observability aggregate
- scheduler/reuse observability aggregate
- scheduler/reuse flow observability aggregate
- scheduler/reuse simulation preview aggregate
- scheduler/reuse simulation preview presenter helper
- presenter/resumo scheduler-reuse simulation preview aggregate
- presenter/resumo scheduler-reuse simulation view/text
- presenter/resumo scheduler-reuse simulation text presenter
- scheduler/reuse debug telemetry packet
- scheduler/reuse full chain assembly helper

Representative files:

- `src/game_loop_track_reuse_decision_view_contracts.hpp`
- `src/game_loop_track_reuse_telemetry_view_contracts.hpp`
- `src/game_loop_track_reuse_observability_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_commit_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_commit_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_commit_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_state_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_state_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_decision_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_decision_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_bridge_assembler.hpp`
- `src/game_loop_reuse_runtime_seam_assembler.hpp`
- `src/game_loop_simulation_reuse_decision_view_contracts.hpp`
- `src/game_loop_simulation_reuse_telemetry_view_contracts.hpp`
- `src/game_loop_reuse_observability_contracts.hpp`
- `src/game_loop_reuse_observability_debug_contracts.hpp`
- `src/game_loop_reuse_observability_debug_presenter_ops.hpp`
- `src/game_loop_reuse_observability_debug_bundle_contracts.hpp`
- `src/game_loop_simulation_drain_view_contracts.hpp`
- `src/game_loop_simulation_completion_view_contracts.hpp`
- `src/game_loop_simulation_scheduler_telemetry_view_contracts.hpp`
- `src/game_loop_simulation_scheduler_lifecycle_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_flow_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_debug_telemetry_contracts.hpp`

Removed during later cleanup:

- `src/game_loop_track_reuse_preview_contracts.hpp`
- `src/game_loop_track_reuse_preview_presenter_ops.hpp`
- `src/game_loop_track_reuse_runtime_bridge_assembler.hpp`
- `src/game_loop_track_reuse_runtime_preview_presenter_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_presenter_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_bridge_presenter_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_bridge_presenter_ops.hpp`
- `src/game_loop_scheduler_reuse_observability_assembly_ops.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_assembler.hpp`
- `src/game_loop_simulation_reuse_runtime_debug_preview_presenter_ops.hpp`
- `src/game_loop_scheduler_reuse_simulation_preview_contracts.hpp`
- `src/game_loop_scheduler_reuse_simulation_preview_assembler.hpp`
- `src/game_loop_scheduler_reuse_simulation_preview_presenter_ops.hpp`

Runtime status:

- two narrow live observability substitutions are now active
- one local live observability consumer now assembles:
  - `SimulationSchedulerTelemetryViewPacket`
- `TrackRenderProducerStatePacket` is now consumed by a separate local
  presentation/debug helper without changing the SH2 snapshot layout
- one additional narrow live track-only reuse seam is now active:
  - one local `TrackReuseRuntimeState` is captured in the host track flow
  - one real track-only `FrameReuseRuntimeOwnerPacket` is built on demand in
    `TryBuildReuseObservabilityDebugBundle(...)`
  - simulation-side reuse remains neutral there
- one additional passive helper now exists for the next symmetric simulation-side
  seam completion:
  - authoritative `SimulationPayload` -> `SimulationReuseRuntimeCommitPacket`
  - authoritative `SimulationPayload` -> `SimulationFrameHistoryState` commit
- one sibling passive state helper now exists for that same seam:
  - `SimulationReuseRuntimeState` persists only
    `SimulationFrameHistoryState`
  - one narrow owner-packet builder can surface that state without adding
    broader scheduler ownership
- one narrow combined seam helper now exists directly above the accepted
  observability-only retry point:
  - simulation-side request inputs + simulation history
  - track-side request state + track history
  - one combined `FrameReuseRuntimeOwnerPacket`
- one additional passive decision bridge now exists for that same future seam:
  - exact local host inputs -> `SimulationReuseDecisionPacket`
  - exact local host inputs -> `SimulationReuseDecisionViewPacket`
- one sibling compile-only preview now exists above that same decision bridge:
  - exact local host inputs
  - derived `SimulationReuseDecisionViewPacket`
  - no runtime owner-packet change required
- one sibling presenter helper now exists above that preview:
  - one narrow debug line for the derived simulation-side decision view only
- one sibling presenter bridge now also exists above that helper:
  - exact host decision inputs -> preview packet -> preview presenter
- one local observability/debug helper now also exists above that bridge:
  - exact host decision inputs -> simulation preview debug presentation
- one explicit local observability boundary now also exists above that helper:
  - local debug preview packet
  - local debug preview presenter
  - no seam or owner-packet dependency
- one higher compile-only scheduler/reuse preview now also exists above that
  local boundary:
  - `SchedulerReuseFlowObservabilityPacket`
  - `SimulationReuseRuntimeDebugPreviewPacket`
- that aggregate is now materialized as one explicit preview packet above the
  local simulation-side debug boundary
- one sibling presenter helper now also exists above that aggregate
- prepared to keep future track/simulation reuse substitutions symmetric
- no live scheduler ownership change has been introduced here
- one compile-only helper now assembles the full lower-to-higher
  scheduler/reuse chain off-path
- one derived compile-only debug view now exists directly above
  `ReuseObservabilityPacket`
- one compile-only presenter helper now exists directly above that debug view
- one compile-only local bundle now exists directly above that presenter/helper
- one compile-only local bundle presenter now exists directly above that local
  bundle
- the failed broader simulation-side retry is now explicitly blocked by runtime
  boot stability; see `SIMULATION_REUSE_LIVE_RETRY_BLOCKER.md`
- the preferred next narrow candidate is now a preview-first reopen above the
  simulation decision branch, before any combined owner-packet live retry
- the broader symmetric retry still carries the measured `+4096` byte code-side
  budget pressure
- use `DEBUG_TELEMETRY_SIZE_REDUCTION_PLAN.md` and the accepted track-only seam
  as the reference baseline before reopening it
- first reduction pass already collapsed duplicate packet-overload glue in the
  compile-only reuse presenter/helper stack
- second reduction pass already compressed the reuse debug presenter output from
  two lines into one compact line
- third reduction pass already removed the extra compile-only local helper
  layer above the bundle presenter
- a post-reduction broader decision-first symmetric live retry was attempted
  and still reproduced the same `+4096` byte ISO regression, so only the new
  track-only low seam remains live today

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
- `MEMORY_BUDGET_PRESENTATION_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `MEMORY_BUDGET_LOW_WORK_OVERLAY_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_FRAME_END_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_RUNTIME_BRIDGE_BOUNDARY_CONSOLIDATED.md`

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

- former passive memory-budget frame packet pair removed after smoke validation
  stopped depending on it
- `src/game_loop_low_work_overlay_capture_ops.hpp`
- `src/game_loop_low_work_overlay_assembly_ops.hpp`
- `src/game_loop_memory_debug_presenter_ops.hpp`
- `src/game_loop_low_work_overlay_presenter_ops.hpp`
- `src/memory_budget_contracts.hpp`
- `src/memory_budget_runtime_bridge.hpp`
- `src/game_loop_render_budget_observability_view_contracts.hpp`
- `src/game_loop_render_budget_presentation_view_contracts.hpp`
- `src/game_loop_render_budget_overlay_text_view_contracts.hpp`
- `src/game_loop_render_budget_overlay_text_bridge_assembler.hpp`
- `src/game_loop_render_budget_overlay_text_presenter_ops.hpp`
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
- one compile-only render-budget textual boundary now also exists above:
  - `RenderBudgetObservabilityViewPacket`
  - `RenderBudgetPresentationViewPacket`
  - `RenderBudgetOverlayTextViewPacket`
- one sibling compile-only presenter helper now exists directly above that
  textual boundary
- that render-budget textual chain remains intentionally compile-only because
  no remove-first runtime textual consumer exists yet in the active host path

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
3. continue from `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md` above the new
   track-only live seam, completing only the remaining symmetric simulation-side
   branch before any broader scheduler/reuse aggregate
4. keep bootstrap/presenter changes compile-only unless a remove-first patch is obvious
