# Presenter Passive Refactor Inventory

## Objective

Provide one consolidated inventory of the passive presenter/observability/render
refactor already completed, so the next runtime step can be chosen without
re-reading multiple plans.

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
- `src/game_loop_track_render_debug_assembler.hpp`
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

Main packet:

- `PresenterInputBundle`

Purpose:

- keep one full presenter-facing aggregate packet
- preserve access to both broad and narrowed passive slices in one place

Current contents:

- `PresentationDebugBundle`
- `RenderFrameDebugBundle`
- `PresenterRenderDebugPacket`
- `OverlayDebugBundle`
- `PresenterOverlayDebugPacket`
- `ObservabilityDebugBundle`
- `PresenterInputSummaryPacket`

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
- memory debug presence
- speed/gear/rpm
- face counters
- query counters
- frame id

### 8. Facade handoff layer

Files:

- `src/game_loop_presenter_facade_contracts.hpp`
- `src/game_loop_presenter_facade_assembler.hpp`

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

### 10. Facade bridge layer

Files:

- `src/game_loop_presenter_facade_bridge_contracts.hpp`
- `src/game_loop_presenter_facade_bridge_assembler.hpp`

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

### 11. Frame-end decision layer

Files:

- `src/game_loop_presenter_frame_end_decision_contracts.hpp`
- `src/game_loop_presenter_frame_end_decision_assembler.hpp`

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

### 12. HUD/telemetry decision layer

Files:

- `src/game_loop_presenter_hud_telemetry_decision_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_assembler.hpp`

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
- `MEMORY_BUDGET_PASSIVE_FLOW_PLAN.md`
