# Presenter Passive Refactor Inventory

## Objective

Provide one consolidated inventory of the passive presenter/observability/render
refactor already completed, so the next runtime step can be chosen without
re-reading multiple plans.

Branch status note:

- the older `PresenterFacade*`, `PresenterFrameEndDecision*`, and
  `PresenterHudTelemetryDecision*` chains described later in this document are
  historical/archival in this branch
- their source headers are no longer present under `src/`
- the current highest presenter compile-only boundary still present in source is
  `PresenterSummaryObservabilityPacket`

## Scope

This inventory covers only the passive presenter-facing groundwork around:

- presentation/HUD
- render debug
- overlay/observability debug
- facade handoff contracts

It does not change runtime ownership.

## Passive layers

### 1. Presentation layer

Files:

- `src/game_loop_presentation_debug_contracts.hpp`
- `src/game_loop_presentation_debug_assembler.hpp`

Main packets:

- `DrivingHudTextPacket`
- `PeriodicHudStatsPacket`
- `PresentationDebugBundle`

Purpose:

- group HUD/presentation data outside `src/game_loop_system.hpp`
- keep speed/gear/rpm and periodic HUD telemetry available as passive inputs

### 2. Render aggregate layer

Files:

- `src/game_loop_track_render_packet.hpp`
- `src/game_loop_track_render_packet_assembler.hpp`
- `src/game_loop_car_visual_packet.hpp`
- `src/game_loop_car_visual_packet_assembler.hpp`
- `src/game_loop_render_debug_contracts.hpp`
- `src/game_loop_render_debug_assembler.hpp`

Main packets:

- `TrackRenderFramePacket`
- `CarVisualFramePacket`
- `RenderFrameDebugBundle`

Purpose:

- keep full passive render-side packets grouped frame-locally
- preserve future access to full car/track packet detail without runtime coupling

### 3. Render reduced summaries

Files:

- `src/game_loop_track_render_debug_contracts.hpp`
- former wrapper `src/game_loop_track_render_debug_assembler.hpp`
- `src/game_loop_car_visual_debug_contracts.hpp`
- `src/game_loop_car_visual_debug_assembler.hpp`
- `src/game_loop_presenter_render_debug_contracts.hpp`
- `src/game_loop_presenter_render_debug_assembler.hpp`

Main packets:

- `TrackRenderDebugPacket`
- `CarVisualDebugPacket`
- `PresenterRenderDebugPacket`

Purpose:

- expose reduced render-side summaries for presenter/debug consumers
- avoid walking full track/car render packets when only debug-level decisions are needed

### 4. Overlay and observability aggregate layer

Files:

- `src/game_loop_overlay_debug_text_contracts.hpp`
- `src/game_loop_overlay_debug_text_assembler.hpp`
- `src/game_loop_overlay_debug_contracts.hpp`
- `src/game_loop_overlay_debug_packet_assembler.hpp`
- `src/game_loop_memory_debug_contracts.hpp`
- `src/game_loop_memory_debug_packet_assembler.hpp`
- `src/game_loop_observability_debug_contracts.hpp`
- `src/game_loop_observability_debug_packet_assembler.hpp`

Main packets:

- `OverlayDebugTextBundle`
- `OverlayDebugBundle`
- `MemoryDebugPresentationBundle`
- `ObservabilityDebugBundle`

Purpose:

- keep overlay, telemetry, and memory/debug presentation grouped off-path
- preserve a single passive entry into deeper observability state

### 5. Overlay reduced summary

Files:

- `src/game_loop_presenter_overlay_debug_contracts.hpp`
- `src/game_loop_presenter_overlay_debug_assembler.hpp`

Main packet:

- `PresenterOverlayDebugPacket`

Purpose:

- expose reduced overlay/observability presence and counters
- avoid walking both `OverlayDebugBundle` and `ObservabilityDebugBundle` for first-level presenter decisions

### 6. Presenter aggregate layer

Files:

- `src/game_loop_presenter_input_contracts.hpp`
- `src/game_loop_presenter_input_assembler.hpp`
- `src/game_loop_presenter_observability_input_contracts.hpp`
- `src/game_loop_presenter_observability_input_assembler.hpp`
- `src/game_loop_presenter_scheduler_reuse_bridge_assembler.hpp`
- `src/game_loop_presenter_scheduler_reuse_preview_contracts.hpp`
- `src/game_loop_presenter_scheduler_reuse_preview_assembler.hpp`

Main packet:

- `PresenterInputBundle`

Purpose:

- keep one full presenter-facing aggregate packet
- preserve access to both broad and narrowed passive slices in one place

Current contents:

- `PresentationDebugBundle`
- `RenderFrameDebugBundle`
- `PresenterObservabilityInputPacket`
- `PresenterRenderDebugPacket`
- `OverlayDebugBundle`
- `PresenterOverlayDebugPacket`
- `ObservabilityDebugBundle`
- `PresenterInputSummaryPacket`

Additional purpose:

- preserve one future-ready observability-side adapter for presenter input
- keep scheduler/reuse debug telemetry attachable to presenter input without
  widening ownership in runtime
- keep a sibling compile-only bridge that derives scheduler/reuse debug input
  from scheduler telemetry + producer flags + `frame_reuse` runtime-owner state
  without reopening `src/game_loop_system.hpp`
- keep one compile-only preview directly above that sibling bridge so the
  presenter summary side can validate this attach point without runtime changes

### 7. Presenter top-level summary

Files:

- `src/game_loop_presenter_input_summary_contracts.hpp`
- `src/game_loop_presenter_input_summary_assembler.hpp`

Main packet:

- `PresenterInputSummaryPacket`

Purpose:

- expose top-level presence and counters
- let a future facade branch before touching deeper packets

Current summary fields:

- presentation/HUD presence
- render/overlay/observability presence
- scheduler/reuse debug presence
- memory debug presence
- speed/gear/rpm
- face counters
- query counters
- frame id

### 7.5. Current top presenter boundary

Files:

- `src/game_loop_presenter_summary_observability_contracts.hpp`
- `src/game_loop_presenter_summary_observability_assembler.hpp`

Main packet:

- `PresenterSummaryObservabilityPacket`

Purpose:

- keep the current highest presenter compile-only boundary aligned to files that
  still exist in `src/`
- group the enriched top-level summary with the narrowed observability input
  without reviving removed facade/decision shims

### 7.6. Current presenter presence-decision boundary

Files:

- `src/game_loop_presenter_presence_decision_contracts.hpp`
- `src/game_loop_presenter_presence_decision_assembler.hpp`

Main packet:

- `PresenterPresenceDecisionPacket`

Purpose:

- expose the narrowest current presenter decision surface that still exists in
  `src/`
- derive direct presence decisions from `PresenterSummaryObservabilityPacket`
  without reviving the removed facade/frame-end chains

### 7.7. Current presenter presence preview

Files:

- `src/game_loop_presenter_presence_preview_contracts.hpp`
- `src/game_loop_presenter_presence_preview_assembler.hpp`

Main packet:

- `PresenterPresencePreviewPacket`

Purpose:

- keep one compile-only preview directly above the current presenter
  presence-decision boundary
- validate the local chain
  `PresenterInputBundle -> PresenterSummaryObservabilityPacket ->
  PresenterPresenceDecisionPacket`
  without reviving removed facade/frame-end shims

### 7.8. Presenter scheduler/reuse summary preview

Files:

- `src/game_loop_presenter_scheduler_reuse_preview_contracts.hpp`
- `src/game_loop_presenter_scheduler_reuse_preview_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_preview_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_preview_assembler.hpp`

Main packet:

- `PresenterSummarySchedulerReusePreviewPacket`

Purpose:

- keep one compile-only preview directly above the scheduler/reuse presenter
  attach point and the presenter summary boundary
- validate the local chain
  `SchedulerReuseDebugTelemetryPacket -> PresenterObservabilityInputPacket ->
  PresenterSummaryObservabilityPacket`
  without runtime changes

### 7.9. Presenter top boundary preview

Files:

- `src/game_loop_presenter_boundary_preview_contracts.hpp`
- `src/game_loop_presenter_boundary_preview_assembler.hpp`

Main packet:

- `PresenterBoundaryPreviewPacket`

Purpose:

- keep one compile-only packet at the current highest presenter boundary that
  still exists in this branch
- group the current presenter local-decision branch and the current
  scheduler/reuse summary branch in one place
- validate the local chain
  `PresenterPresencePreviewPacket + PresenterSummarySchedulerReusePreviewPacket`
  without runtime changes

### 7.10. Presenter top boundary view

Files:

- `src/game_loop_presenter_boundary_view_contracts.hpp`
- `src/game_loop_presenter_boundary_view_assembler.hpp`

Main packet:

- `PresenterBoundaryViewPacket`

Purpose:

- expose one minimal presenter-side attach packet above the current top preview
- keep only the small subset useful for a future non-critical textual helper:
  presence decisions, observability/scheduler-reuse flags, speed/gear/rpm, and
  frame id
- avoid carrying the broader nested preview hierarchy into the next compile-only
  consumer

Additional compile-only consumer:

- `src/game_loop_presenter_boundary_text_contracts.hpp`
- `src/game_loop_presenter_boundary_text_assembler.hpp`
- `src/game_loop_presenter_boundary_view_presenter_ops.hpp`

Purpose:

- provide one textual packet layer above `PresenterBoundaryViewPacket`
- provide one narrow textual presenter helper above `PresenterBoundaryViewPacket`
- keep any future retry localized to this view packet instead of the broader
  preview hierarchy

Minimal future runtime reopening plan:

- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PRESENTER_BOUNDARY_TEXT_FIRST_STATUS_RETRY_PATCH_PLAN.md`
- `PRESENTER_BOUNDARY_DECISION_RETRY_BLOCKER.md`

### 8. Facade handoff layer

Files:

- `src/game_loop_presenter_facade_contracts.hpp`
- `src/game_loop_presenter_facade_assembler.hpp`
- `src/game_loop_presenter_facade_input_contracts.hpp`
- `src/game_loop_presenter_facade_input_assembler.hpp`
- `src/game_loop_presenter_facade_decision_input_contracts.hpp`
- `src/game_loop_presenter_facade_decision_input_assembler.hpp`
- `src/game_loop_presenter_facade_decision_bridge_contracts.hpp`
- `src/game_loop_presenter_facade_decision_bridge_assembler.hpp`

Main packet:

- `PresenterFacadePacket`

Purpose:

- define the directly consumable handoff into a future passive presenter facade

Current contents:

- `PresenterInputSummaryPacket`
- `DrivingHudTextPacket`
- `PeriodicHudStatsPacket`
- `PresenterRenderDebugPacket`
- `PresenterOverlayDebugPacket`

Additional purpose:

- provide one narrower facade-ready input path above
  `PresenterObservabilityInputPacket`
- let future facade assembly avoid depending on the full
  `PresenterInputBundle`

### 9. Facade interface layer

Files:

- `src/game_loop_presenter_facade_interface_contracts.hpp`
- `src/game_loop_presenter_facade_interface_assembler.hpp`
- `GAME_LOOP_PRESENTER_FACADE_PLAN.md`

Main packets:

- `PresenterFacadeRequestPacket`
- `PresenterFacadeDecisionPacket`

Purpose:

- define the explicit request/decision contract of a future `GameLoopPresenterFacade`
- separate contract-level runtime preparation from actual runtime ownership changes

Current decision surface:

- `shouldPresentHud`
- `shouldPresentPeriodicHud`
- `shouldPresentRenderDebug`
- `shouldPresentOverlayDebug`
- `shouldPresentMemoryDebug`

Additional purpose:

- allow future facade decisions to be derived from a minimal packet without
  carrying the full facade packet into the decision boundary
- allow future HUD/frame-end boundaries to share one narrow compile-only bridge
  above the minimal decision input

### 10. Facade bridge layer

Files:

- `src/game_loop_presenter_facade_bridge_contracts.hpp`
- `src/game_loop_presenter_facade_bridge_assembler.hpp`
- `src/game_loop_presenter_compile_only_preview_contracts.hpp`
- `src/game_loop_presenter_compile_only_preview_assembler.hpp`

Main packet:

- `PresenterFacadeBridgePacket`

Purpose:

- preserve the full passive bridge from input-facing facade data into request and
  decision packets
- validate the entire chain off-path before any new live runtime substitution

Current contents:

- `PresenterFacadePacket`
- `PresenterFacadeRequestPacket`
- `PresenterFacadeDecisionPacket`

Additional compile-only entry:

- `PresenterFacadeDecisionInputPacket`
- `PresenterFacadeDecisionBridgePacket`

### 11. Compile-only preview layer

Files:

- `src/game_loop_presenter_compile_only_preview_contracts.hpp`
- `src/game_loop_presenter_compile_only_preview_assembler.hpp`

Main packet:

- `PresenterCompileOnlyPreviewPacket`

Purpose:

- validate one off-path passive preview that groups:
  - `PresenterFacadeBridgePacket`
  - `PresenterFacadeDecisionBridgePacket`
- keep compile-only bridge coverage explicit before any runtime retry

### 12. Frame-end decision layer

Files:

- `src/game_loop_presenter_frame_end_decision_contracts.hpp`
- `src/game_loop_presenter_frame_end_decision_assembler.hpp`
- `src/game_loop_presenter_frame_end_preview_contracts.hpp`
- `src/game_loop_presenter_frame_end_preview_assembler.hpp`

Main packet:

- `PresenterFrameEndDecisionPacket`

Purpose:

- provide the narrowest passive HUD/overlay decision packet currently needed for
  a future presentation boundary
- keep the future live call site focused on direct end-of-frame flags instead of
  the broader facade chain

Current contents:

- `shouldPresentDrivingHud`
- `shouldPresentPeriodicHud`
- `shouldPresentOverlayDebug`
- `shouldPresentMemoryDebug`

Additional compile-only preview:

- `PresenterFrameEndPreviewPacket`

### 13. HUD/telemetry decision layer

Files:

- `src/game_loop_presenter_hud_telemetry_decision_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_input_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_assembler.hpp`
- `src/game_loop_presenter_hud_telemetry_preview_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_preview_assembler.hpp`

Main packet:

- `PresenterHudTelemetryDecisionPacket`

Purpose:

- provide the narrowest passive packet for the
  `PresentFrameHudAndTelemetry(...)` boundary
- preserve the current split between periodic HUD gating and runtime telemetry
  gating

Current contents:

- `shouldPresentPeriodicHud`
- `shouldPresentSegmentOverlapDiagnostics`
- `shouldPresentSh2Telemetry`

Input dependency packet:

- `PresenterHudTelemetryDecisionInputPacket`

Additional compile-only preview:

- `PresenterHudTelemetryPreviewPacket`

## Validation coverage

Compile-only SH2 hooks validate the passive include chain:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

Stable build validation:

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

Current invariant:

- ISO must remain exactly `4134912`

## Runtime status

Current runtime integration status:

- no presenter passive packet is used in live runtime
- `src/game_loop_system.hpp` ownership remains unchanged
- no HUD, overlay, audio, scheduler, or pacing behavior changed
- the first attempted runtime boundary at `src/game_loop_system.hpp:2077`
  was rolled back after emulator instability
- the facade bridge is compile-only and not consumed by runtime
- the frame-end decision packet is compile-only and not consumed by runtime
- the HUD/telemetry decision packet is compile-only and not consumed by runtime

## Recommended first runtime move

The first attempted runtime move in `UpdateFrameEndOverlays()` is now considered
failed-first and documented in
`GAME_LOOP_PRESENTER_RUNTIME_ALTERNATIVES.md`.

The next acceptable runtime move should be remove-first and local:

1. keep `GameLoopSystem` as owner
2. assemble `PresenterInputBundle` stack-locally
3. assemble `PresenterFacadePacket`
4. assemble `PresenterFacadeDecisionPacket`
5. branch only on presence flags
6. prefer `PresentFrameHudAndTelemetry(...)` before retrying
   `UpdateFrameEndOverlays()`
7. keep existing print/update call sites unchanged in the same patch

## Do not do next

Do not do these as the first runtime presenter patch:

- move `SRL::Debug::Print(...)` ownership
- create a long-lived presenter member in `GameLoopSystem`
- add heap allocations or caches
- mix presenter integration with scheduler/audio/simulation changes
- replace multiple formatting paths in a single patch

## Reference plans

- `GAME_LOOP_OBSERVABILITY_FLOW_PLAN.md`
- `GAME_LOOP_PRESENTER_FACADE_PLAN.md`
- `GAME_LOOP_PRESENTER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `GAME_LOOP_PRESENTER_RUNTIME_MINIMAL_INTEGRATION_PLAN.md`
- `GAME_LOOP_PRESENTER_RUNTIME_ALTERNATIVES.md`
- `SIMULATION_REUSE_RUNTIME_DEBUG_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `SCHEDULER_REUSE_SIMULATION_PREVIEW_BOUNDARY_CONSOLIDATED.md`
- `MEMORY_BUDGET_PASSIVE_FLOW_PLAN.md`
