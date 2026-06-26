# Game Loop Presenter Facade Plan

## Objective

Prepare a future passive `GameLoopPresenterFacade` extraction out of
`src/game_loop_system.hpp` without changing current HUD, overlay, debug, or
frame pacing behavior.

Companion inventory:

- `PRESENTER_PASSIVE_REFACTOR_INVENTORY.md`
- `PRESENTER_FACADE_CHAIN_FLOW_PLAN.md`
- `PRESENTER_FACADE_LIVE_INTEGRATION_INVENTORY.md`
- `GAME_LOOP_PRESENTER_RUNTIME_MINIMAL_INTEGRATION_PLAN.md`
- `GAME_LOOP_PRESENTER_RUNTIME_ALTERNATIVES.md`
- `GAME_LOOP_PRESENTER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

## Current passive inputs

The presenter-facing passive groundwork now exists in:

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
- `src/game_loop_presenter_facade_decision_input_contracts.hpp`
- `src/game_loop_presenter_facade_decision_input_assembler.hpp`
- `src/game_loop_presenter_facade_decision_bridge_contracts.hpp`
- `src/game_loop_presenter_facade_decision_bridge_assembler.hpp`
- `src/game_loop_presenter_facade_interface_contracts.hpp`
- `src/game_loop_presenter_facade_interface_assembler.hpp`
- `src/game_loop_presenter_facade_bridge_contracts.hpp`
- `src/game_loop_presenter_facade_bridge_assembler.hpp`
- `src/game_loop_presenter_frame_end_decision_contracts.hpp`
- `src/game_loop_presenter_frame_end_decision_assembler.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_contracts.hpp`
- `src/game_loop_presenter_hud_telemetry_decision_assembler.hpp`

## Current passive model

### Aggregate input

`PresenterInputBundle` carries the full passive presenter-facing input:

- presentation/HUD bundle
- render aggregate bundle
- render reduced summary
- overlay aggregate bundle
- overlay reduced summary
- observability aggregate bundle
- top-level input summary

### Facade-ready handoff

`PresenterFacadePacket` narrows the aggregate to what a passive presenter should
need first:

- `PresenterInputSummaryPacket`
- `DrivingHudTextPacket`
- `PeriodicHudStatsPacket`
- `PresenterRenderDebugPacket`
- `PresenterOverlayDebugPacket`

### Explicit facade interface

`PresenterFacadeRequestPacket` and `PresenterFacadeDecisionPacket` now define the
expected non-owning handoff shape for a future facade:

- request carries one `PresenterFacadePacket`
- decision carries only:
  - phase hint
  - HUD present flag
  - periodic HUD present flag
  - render debug present flag
  - overlay debug present flag
  - memory debug present flag

This keeps the future facade:

- frame-local
- allocation-free
- side-effect-free until explicitly connected

### Minimal decision input

`PresenterFacadeDecisionInputPacket` now defines the narrowest compile-only
input required to derive `PresenterFacadeDecisionPacket`:

- `hasDrivingHud`
- `hasPeriodicHud`
- `hasRenderDebug`
- `hasOverlayDebug`
- `hasMemoryDebug`

It exists to:

- narrow the future decision boundary below the full facade packet
- let future live retries substitute decision booleans without dragging the
  broader facade handoff into the same call site

### Minimal decision bridge

`PresenterFacadeDecisionBridgePacket` now groups the smallest compile-only
decision chain needed by the two known live presenter boundaries:

- `PresenterFacadeDecisionInputPacket`
- `PresenterHudTelemetryDecisionInputPacket`
- `PresenterFacadeDecisionPacket`
- `PresenterFrameEndDecisionPacket`
- `PresenterHudTelemetryDecisionPacket`

It exists to:

- prepare one narrow boundary-local bridge above the minimal decision input
- let future live retries target `PresentFrameHudAndTelemetry(...)` and
  `UpdateFrameEndOverlays()` without carrying the broader facade/request chain

### Compile-only bridge

`PresenterFacadeBridgePacket` now preserves the complete passive chain:

- `PresenterFacadePacket`
- `PresenterFacadeRequestPacket`
- `PresenterFacadeDecisionPacket`

This bridge is intentionally off-path today.

It exists to:

- validate the full passive handoff chain in compile-only smoke checks
- keep the future live substitution smaller
- avoid reconstructing request/decision glue inside `src/game_loop_system.hpp`

### Narrow frame-end decision

`PresenterFrameEndDecisionPacket` now narrows the bridge down to the smallest
HUD/overlay decision surface currently worth preparing off-path:

- `shouldPresentDrivingHud`
- `shouldPresentPeriodicHud`
- `shouldPresentOverlayDebug`
- `shouldPresentMemoryDebug`

It exists to:

- prepare a future frame-end substitution with fewer dependencies than the full
  facade/request/decision chain
- reduce the amount of passive state that a live boundary would need to consume
- keep the future runtime patch focused on direct presentation flags only

### Narrow HUD/telemetry decision

`PresenterHudTelemetryDecisionPacket` now narrows the passive decision surface
for `PresentFrameHudAndTelemetry(...)` down to the smallest flags that match the
current runtime behavior:

- `shouldPresentPeriodicHud`
- `shouldPresentSegmentOverlapDiagnostics`
- `shouldPresentSh2Telemetry`

It exists to:

- prepare a future substitution on the HUD/telemetry boundary without carrying
  the broader presenter chain into the live call site
- keep optional HUD telemetry gating separate from runtime stats gating
- mirror the current runtime branches with a small packet instead of ad hoc
  boolean recomputation

## Safe runtime substitution order

### Step 1 - keep `GameLoopSystem` ownership

Do not move:

- `SRL::Debug::Print(...)` ownership
- HUD text update ordering
- overlay update ordering
- realtime FPS ordering
- any frame pacing branches

### Step 2 - local passive assembly only

When runtime integration becomes safe, assemble locally in the same scope:

1. `PresenterInputBundle`
2. `PresenterFacadePacket`
3. `PresenterFacadeRequestPacket`
4. `PresenterFacadeDecisionPacket`

Consume them immediately inside `src/game_loop_system.hpp`.

No new persistent members.
No heap allocation.
No new cross-frame cache.

### Step 3 - branch on facade decision only

The first runtime use of the facade should not print anything by itself.

It should only decide:

- whether HUD payloads are present
- whether periodic HUD payloads are present
- whether render debug payloads are present
- whether overlay/memory debug payloads are present

Actual printing remains in existing call sites in the same patch.

### Step 4 - move formatting later

Only after repeated stable emulator runs:

- route one textual presenter helper through `PresenterFacadePacket`
- keep the existing direct print helpers as fallback until stable

### Step 5 - isolate facade object last

Only after steps 1-4 are stable:

- introduce a non-owning `GameLoopPresenterFacade`
- keep it passive and stack-local at first
- only then reduce the host-local formatting spread

## Guard rails

- do not mix this with audio, scheduler, or simulation changes
- do not grow `src/game_loop_system.hpp` unless equivalent runtime code is removed
- do not introduce dynamic allocation
- validate every step with `tools/validate_saturn_stable_build.ps1`
- keep ISO exactly `4134912`

## Immediate next safe step

The next safe step is runtime-neutral preparation only.

The first attempted live boundary in `UpdateFrameEndOverlays()` is now treated
as failed-first and should not be retried ahead of the alternative route
described in `GAME_LOOP_PRESENTER_RUNTIME_ALTERNATIVES.md`.

Current safe choices:

1. keep all runtime behavior unchanged
2. optionally assemble the facade packet off-path for compile-time validation only
3. if live integration is retried, prefer `PresentFrameHudAndTelemetry(...)`
   before `UpdateFrameEndOverlays()`
4. defer any live call-site usage until an equivalent remove-first patch is chosen
5. when live substitution is retried, prefer the narrow-packet order defined in
   `GAME_LOOP_PRESENTER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
