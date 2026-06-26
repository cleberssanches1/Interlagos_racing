# Presenter / Facade Live Integration Inventory

## Objective

Document the exact live integration status of the presenter/facade chain and
the only acceptable future runtime substitution shapes.

This document is runtime-facing inventory only.

It does not authorize a live patch by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- presenter runtime ownership remains in `src/game_loop_system.hpp`

## Current live status

The presenter/facade chain is still compile-only.

No passive presenter packet is consumed live in:

- `src/game_loop_system.hpp`

Current live HUD/debug ownership remains in the host:

- `PresentFrameHudAndTelemetry(...)`
- `UpdateFrameEndOverlays()`
- current `SRL::Debug::Print(...)` call sites
- current HUD system submission call sites

## Current passive chain available

The chain now exists in these narrowing layers:

1. `PresenterInputBundle`
2. `PresenterObservabilityInputPacket`
3. `PresenterInputSummaryPacket`
4. `PresenterFacadeInputPacket`
5. `PresenterFacadePacket`
6. `PresenterFacadeDecisionInputPacket`
7. `PresenterFacadeDecisionPacket`
8. `PresenterFacadeDecisionBridgePacket`
9. `PresenterFrameEndDecisionPacket`
10. `PresenterHudTelemetryDecisionInputPacket`
11. `PresenterHudTelemetryDecisionPacket`

## Runtime boundaries already prepared

### Boundary A - `PresentFrameHudAndTelemetry(...)`

Target type:

- narrow boolean gating only

Prepared packets:

1. `PresenterFacadeDecisionInputPacket`
2. `PresenterHudTelemetryDecisionInputPacket`
3. `PresenterFacadeDecisionBridgePacket`
4. `PresenterHudTelemetryDecisionPacket`

Current allowed live use:

- none

First acceptable live substitution:

- replace only equivalent local gating booleans
- keep print/hud ownership in place

### Boundary B - `UpdateFrameEndOverlays()`

Target type:

- narrow boolean gating only

Prepared packets:

1. `PresenterFacadeDecisionInputPacket`
2. `PresenterFacadeDecisionBridgePacket`
3. `PresenterFrameEndDecisionPacket`

Current allowed live use:

- none

First acceptable live substitution:

- replace only equivalent local gating booleans
- keep helper/update ordering in place

## Explicitly non-live layers

The following are prepared but must not be the first runtime consumer:

- `PresenterInputBundle`
- `PresenterObservabilityInputPacket`
- `PresenterFacadeInputPacket`
- `PresenterFacadePacket`
- `PresenterFacadeRequestPacket`
- `PresenterFacadeBridgePacket`

These remain useful upstream/off-path, but they are too broad for the first
retry on a critical boundary.

## Remove-first rule

Any future live presenter patch must:

1. target one boundary only
2. consume only the narrow packet prepared for that boundary
3. remove equivalent host-local boolean gating in the same patch
4. keep call order unchanged
5. keep formatting ownership unchanged

## Current prohibited live moves

Do not do these in the first live retry:

- move `SRL::Debug::Print(...)` ownership
- introduce a long-lived presenter object
- replace multiple presenter boundaries in one patch
- mix presenter live integration with scheduler/audio/simulation changes
- consume broad facade/input packets directly in the host boundary

## Acceptance criteria for a future live retry

Every future presenter live patch must keep:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- no HUD ordering drift
- no overlay ordering drift
- no frame pacing drift

## Validation ritual

Required after every future live attempt:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Related documents

- `PRESENTER_FACADE_CHAIN_FLOW_PLAN.md`
- `PRESENTER_PASSIVE_REFACTOR_INVENTORY.md`
- `GAME_LOOP_PRESENTER_FACADE_PLAN.md`
- `GAME_LOOP_PRESENTER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
