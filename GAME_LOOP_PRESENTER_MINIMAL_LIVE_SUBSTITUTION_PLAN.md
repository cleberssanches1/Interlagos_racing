# Game Loop Presenter Minimal Live Substitution Plan

## Objective

Define the smallest acceptable live substitution order for presenter decisions,
using only the already prepared narrow passive packets and preserving emulator
stability.

This plan exists because the broader live presenter hookup already failed once
when introduced directly into the runtime path.

## Runtime boundaries covered

Only these two boundaries are in scope:

- `src/game_loop_system.hpp:2031` `PresentFrameHudAndTelemetry(...)`
- `src/game_loop_system.hpp:2077` `UpdateFrameEndOverlays()`

No other live presenter integration should happen in the same patch series.

## Narrow packets to use

### HUD/telemetry boundary

Use only:

- `PresenterHudTelemetryDecisionPacket`

Current decision fields:

- `shouldPresentPeriodicHud`
- `shouldPresentSegmentOverlapDiagnostics`
- `shouldPresentSh2Telemetry`

### Frame-end boundary

Use only:

- `PresenterFrameEndDecisionPacket`

Current decision fields:

- `shouldPresentDrivingHud`
- `shouldPresentPeriodicHud`
- `shouldPresentOverlayDebug`
- `shouldPresentMemoryDebug`

## Required substitution order

### Step 1 - HUD/telemetry boundary first

First live retry, if any, must target only:

- `src/game_loop_system.hpp:2031`

Patch shape:

1. assemble narrow passive data locally
2. consume only `PresenterHudTelemetryDecisionPacket`
3. replace only equivalent local boolean gating
4. keep all existing print/hud call ownership in place

Must remain unchanged:

- call order
- formatting
- ownership of `PrintSegmentOverlapDiagnostics(...)`
- ownership of `PrintSh2SplitTelemetry(...)`
- ownership of `context_.hudSystem->PresentPeriodicFrameStats(...)`

### Step 2 - frame-end boundary second

Only after repeated stable runs from Step 1:

- target `src/game_loop_system.hpp:2077`

Patch shape:

1. assemble narrow passive data locally
2. consume only `PresenterFrameEndDecisionPacket`
3. replace only equivalent local boolean gating
4. keep current helper ownership unchanged

Must remain unchanged:

- ownership of `PrintDrivingHud()`
- ownership of `UpdateRealtimeFpsOverlay()`
- ownership of work RAM overlay helpers
- existing update ordering

## What must not be pulled into the live boundary

Do not reintroduce these directly into the runtime call site first:

- `PresenterInputBundle`
- `PresenterFacadePacket`
- `PresenterFacadeRequestPacket`
- `PresenterFacadeDecisionPacket`
- `PresenterFacadeBridgePacket`

Those structures may remain upstream/off-path, but the live boundary should
consume only the narrow packet prepared for it.

## Remove-first rule

Each live patch must be substitutional.

That means:

- if a narrow packet field replaces a local branch, the original branch must be
  removed in the same patch
- if no branch is removed, the packet must stay compile-only

## Acceptance criteria

Every live patch in this sequence must keep:

- ISO exactly `4134912`
- emulator startup stable
- no invalid opcode
- no silent emulator close
- no HUD/overlay ordering drift
- no frame pacing drift

## Validation ritual

Required after every live attempt:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Abort conditions

Rollback immediately if:

- ISO grows above `4134912`
- emulator no longer boots
- invalid opcode appears
- a boundary needs more than its narrow packet
- ownership starts to move together with decision substitution
