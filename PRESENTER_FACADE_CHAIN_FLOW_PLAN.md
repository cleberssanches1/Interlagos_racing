# Presenter / Facade Chain Flow Plan

## Objective

Consolidate the current passive presenter/facade chain into one document that
shows the narrowing order, intended future live boundaries, and current
compile-only status.

This document is inventory-and-flow only.

It does not authorize runtime ownership changes.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- presenter integration remains compile-only

## Chain overview

The current presenter/facade chain is intentionally layered from broad packets
to narrow decision surfaces:

1. presentation layer
2. render aggregate layer
3. render reduced summaries
4. overlay / observability aggregate layer
5. overlay reduced summary
6. presenter aggregate layer
7. presenter top-level summary
8. facade handoff layer
9. facade interface layer
10. facade bridge layer
11. frame-end decision layer
12. HUD/telemetry decision layer

## Layer-by-layer narrowing

### 1. Presentation layer

Files:

- `src/game_loop_presentation_debug_contracts.hpp`
- `src/game_loop_presentation_debug_assembler.hpp`

Primary packets:

- `DrivingHudTextPacket`
- `PeriodicHudStatsPacket`
- `PresentationDebugBundle`

Purpose:

- keep HUD/presentation data passive and frame-local

### 2. Render aggregate layer

Files:

- `src/game_loop_render_debug_contracts.hpp`
- `src/game_loop_render_debug_assembler.hpp`

Primary packets:

- `TrackRenderFramePacket`
- `CarVisualFramePacket`
- `RenderFrameDebugBundle`

Purpose:

- preserve access to full render-side state before narrowing

### 3. Render reduced summaries

Files:

- `src/game_loop_presenter_render_debug_contracts.hpp`
- `src/game_loop_presenter_render_debug_assembler.hpp`

Primary packet:

- `PresenterRenderDebugPacket`

Purpose:

- avoid carrying full render aggregates into first-level presenter decisions

### 4. Overlay / observability aggregate layer

Files:

- `src/game_loop_overlay_debug_contracts.hpp`
- `src/game_loop_overlay_debug_packet_assembler.hpp`
- `src/game_loop_observability_debug_contracts.hpp`
- `src/game_loop_observability_debug_packet_assembler.hpp`

Primary packets:

- `OverlayDebugBundle`
- `ObservabilityDebugBundle`

Purpose:

- keep broad overlay/debug inputs grouped off-path

### 5. Overlay reduced summary

Files:

- `src/game_loop_presenter_overlay_debug_contracts.hpp`
- `src/game_loop_presenter_overlay_debug_assembler.hpp`

Primary packet:

- `PresenterOverlayDebugPacket`

Purpose:

- expose overlay/debug presence and counters without walking deep bundles

### 6. Presenter aggregate layer

Files:

- `src/game_loop_presenter_input_contracts.hpp`
- `src/game_loop_presenter_input_assembler.hpp`
- `src/game_loop_presenter_observability_input_contracts.hpp`
- `src/game_loop_presenter_observability_input_assembler.hpp`

Primary packets:

- `PresenterInputBundle`
- `PresenterObservabilityInputPacket`

Purpose:

- preserve one broad presenter input
- preserve one narrower observability-side adapter

### 7. Presenter top-level summary

Files:

- `src/game_loop_presenter_input_summary_contracts.hpp`
- `src/game_loop_presenter_input_summary_assembler.hpp`

Primary packet:

- `PresenterInputSummaryPacket`

Purpose:

- branch on top-level presence/counters before touching deeper passive state

### 8. Facade handoff layer

Files:

- `src/game_loop_presenter_facade_contracts.hpp`
- `src/game_loop_presenter_facade_assembler.hpp`
- `src/game_loop_presenter_facade_input_contracts.hpp`
- `src/game_loop_presenter_facade_input_assembler.hpp`

Primary packets:

- `PresenterFacadePacket`
- `PresenterFacadeInputPacket`

Purpose:

- preserve one full facade handoff
- preserve one narrower facade-ready input above presenter input

### 9. Facade interface layer

Files:

- `src/game_loop_presenter_facade_interface_contracts.hpp`
- `src/game_loop_presenter_facade_interface_assembler.hpp`
- `src/game_loop_presenter_facade_decision_input_contracts.hpp`
- `src/game_loop_presenter_facade_decision_input_assembler.hpp`

Primary packets:

- `PresenterFacadeRequestPacket`
- `PresenterFacadeDecisionPacket`
- `PresenterFacadeDecisionInputPacket`

Purpose:

- preserve one full request/decision interface
- preserve one minimal decision input below the facade packet

### 10. Facade bridge layer

Files:

- `src/game_loop_presenter_facade_bridge_contracts.hpp`
- `src/game_loop_presenter_facade_bridge_assembler.hpp`
- `src/game_loop_presenter_facade_decision_bridge_contracts.hpp`
- `src/game_loop_presenter_facade_decision_bridge_assembler.hpp`

Primary packets:

- `PresenterFacadeBridgePacket`
- `PresenterFacadeDecisionBridgePacket`

Purpose:

- preserve the complete chain for compile-only validation
- preserve one minimal bridge focused on the two known live boundaries

### 11. Frame-end decision layer

Files:

- `src/game_loop_presenter_frame_end_decision_contracts.hpp`
- `src/game_loop_presenter_frame_end_decision_assembler.hpp`

Primary packet:

- `PresenterFrameEndDecisionPacket`

Purpose:

- narrow the frame-end boundary to direct HUD/overlay flags

### 12. HUD/telemetry decision layer

Files:

- `src/game_loop_presenter_hud_telemetry_decision_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_input_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_assembler.hpp`

Primary packets:

- `PresenterHudTelemetryDecisionPacket`
- `PresenterHudTelemetryDecisionInputPacket`

Purpose:

- narrow the HUD/telemetry boundary to direct periodic/runtime-telemetry flags

## Boundary-focused summary

### Future `PresentFrameHudAndTelemetry(...)` boundary

Preferred passive chain:

1. `PresenterFacadeDecisionInputPacket`
2. `PresenterHudTelemetryDecisionInputPacket`
3. `PresenterFacadeDecisionBridgePacket`
4. `PresenterHudTelemetryDecisionPacket`

### Future `UpdateFrameEndOverlays()` boundary

Preferred passive chain:

1. `PresenterFacadeDecisionInputPacket`
2. `PresenterFacadeDecisionBridgePacket`
3. `PresenterFrameEndDecisionPacket`

## Current runtime status

- all presenter/facade layers remain compile-only
- no passive presenter packet is consumed live in `src/game_loop_system.hpp`
- no HUD, overlay, memory-debug, scheduler, audio, or pacing behavior changed

## Remove-first rule for future live retries

Any future live retry must:

1. keep `GameLoopSystem` as owner
2. consume only the narrow packet prepared for the target boundary
3. remove equivalent local boolean gating in the same patch
4. keep call order and print ownership unchanged

## Validation

Compile-only SH2 validation:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

Stable build validation:

- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Related documents

- `PRESENTER_PASSIVE_REFACTOR_INVENTORY.md`
- `GAME_LOOP_OBSERVABILITY_FLOW_PLAN.md`
- `GAME_LOOP_PRESENTER_FACADE_PLAN.md`
- `GAME_LOOP_PRESENTER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
