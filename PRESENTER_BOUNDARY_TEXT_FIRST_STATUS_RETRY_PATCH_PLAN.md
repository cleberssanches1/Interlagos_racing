# Presenter Boundary Text First Status Retry Patch Plan

## Objective

Describe the exact first runtime retry for the presenter textual boundary using
only the status line equivalent of `PrintDrivingHud()`.

Status:

- applied successfully in runtime
- later extended safely so the shift line now also routes through
  `DrivingHudTextPacket`

This plan remains remove-first and intentionally avoids reopening the broader
`PresenterBoundaryPreviewPacket -> PresenterBoundaryViewPacket ->
PresenterBoundaryTextPacket` chain inside the host runtime.

## Exact target

Target only:

- `src/game_loop_system.hpp`
- method `PrintDrivingHud()`
- current direct line:
  - `SRL::Debug::Print(0, 12, "KM/H:%d GEAR:%c RPM:%d    ", ...)`

Historical note:

- this document now records the first accepted retry shape
- the shift line constraint here was valid for the first retry only

## Narrow substitution path

Use only this exact path:

1. `DrivingHudTextPacket`
2. `PresenterBoundaryStatusTextPacket`
3. `PresentPresenterBoundaryHudStatusTextPacket(...)`

Prepared compile-only bridge/files:

- `src/game_loop_presentation_debug_assembler.hpp`
- `src/game_loop_presenter_boundary_text_driving_hud_bridge_assembler.hpp`
- `src/game_loop_presenter_boundary_text_hud_presenter_ops.hpp`

## Exact patch shape

Inside `PrintDrivingHud()`:

1. keep the existing `BuildExtendedDrivetrainOverlaySnapshot(...)`
2. build `DrivingHudTextPacket` from that snapshot
3. build `PresenterBoundaryStatusTextPacket` from `DrivingHudTextPacket`
4. call `PresentPresenterBoundaryHudStatusTextPacket(...)`
5. remove the equivalent direct `SRL::Debug::Print(...)` line in the same patch
6. keep the shift-line branch unchanged in the first retry only

## Why this is the correct first retry

- it is substitutional
- it touches only one direct print line
- it reuses an already existing narrow HUD packet
- it does not require assembling the broader presenter observability chain
- it does not move ownership

## What remains intentionally deferred

Do not do these in the first retry:

- build `PresenterBoundaryTextPacket` inside `PrintDrivingHud()`
- attach scheduler/reuse flags at this call site
- replace the shift line
- merge with frame-end or HUD/facade historical chains

## Draft replacement sketch

Conceptually, the first retry should reduce to:

1. `const auto drivetrain = BuildExtendedDrivetrainOverlaySnapshot(...)`
2. `const auto drivingHud = BuildDrivingHudTextPacket(drivetrain)`
3. `PresentPresenterBoundaryHudStatusTextPacket(BuildPresenterBoundaryStatusTextPacket(drivingHud))`

with the existing shift branch left untouched below it.

## Acceptance criteria

- ISO remains `4134912`
- emulator still boots
- output at row `0,12` stays semantically identical
- no change in shift-line behavior in the first retry

## Validation ritual

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`
