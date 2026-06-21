# Game Loop Presenter Runtime Minimal Integration Plan

## Objective

Define the exact scope of the first runtime presenter integration patch, with
remove-first discipline and zero intended behavior change.

This plan is intentionally narrower than the broader presenter facade plan.

## Runtime target

The first runtime patch should touch only the presenter-facing end-of-frame path
in `src/game_loop_system.hpp`.

Primary call sites:

- `src/game_loop_system.hpp:2031` `PresentFrameHudAndTelemetry(...)`
- `src/game_loop_system.hpp:2077` `UpdateFrameEndOverlays()`

Referenced leaf paths that remain owned by `GameLoopSystem` in this patch:

- `src/game_loop_system.hpp:2131` `PrintSegmentOverlapDiagnostics(...)`
- `src/game_loop_system.hpp:2472` `UpdateRealtimeFpsOverlay()`
- `src/game_loop_system.hpp:338` `PrintWorkRamUsageRealtime()`
- `src/game_loop_system.hpp:369` `UpdateLowWorkFreeOverlay()`

## What the first runtime patch is allowed to do

Inside the same end-of-frame scope, the patch may assemble stack-local passive
packets only:

1. `PresentationDebugBundle`
2. `RenderFrameDebugBundle` only if equivalent local ad hoc assembly is removed
3. `OverlayDebugBundle` only if equivalent local ad hoc assembly is removed
4. `ObservabilityDebugBundle` only if equivalent local ad hoc assembly is removed
5. `PresenterInputBundle`
6. `PresenterFacadePacket`
7. `PresenterFacadeRequestPacket`
8. `PresenterFacadeDecisionPacket`

## What the first runtime patch must not do

Do not:

- introduce a `GameLoopPresenterFacade` object yet
- move `SRL::Debug::Print(...)` ownership
- move HUD update ownership
- move realtime FPS update ownership
- move memory overlay ownership
- add persistent presenter members to `GameLoopSystem`
- add heap allocations
- add frame-to-frame caches
- mix with scheduler, audio, or simulation changes

## Exact behavioral contract of the first patch

The patch must remain behavior-neutral.

Allowed effect:

- branch on presenter decision flags only

Required non-effect:

- all existing print/update calls still execute from the same functions
- call ordering stays identical
- no formatting strings change
- no additional prints are introduced
- no prints are removed

## Remove-first rule

The patch must be substitutional, not additive.

That means:

- if a local snapshot or presence decision is re-expressed through a passive
  packet, the equivalent host-local ad hoc condition must be removed in the same patch
- if no equivalent host-local condition can be removed, do not introduce that
  passive assembly into runtime yet

## Recommended exact patch shape

### Phase A - decision-only integration

Safest first patch:

1. assemble `PresentationDebugBundle`
2. assemble `PresenterInputBundle` with already-available narrower summaries
3. assemble `PresenterFacadePacket`
4. assemble `PresenterFacadeDecisionPacket`
5. use decision flags only as mirrors next to the current conditions
6. keep the original conditions executing the actual call sites

This is acceptable only if the mirrored conditions replace equivalent host-local
presence checks in the same patch.

### Phase B - local substitution of presence checks

After Phase A is stable:

1. replace local `valid`/presence branches with:
   - `shouldPresentHud`
   - `shouldPresentPeriodicHud`
   - `shouldPresentRenderDebug`
   - `shouldPresentOverlayDebug`
   - `shouldPresentMemoryDebug`
2. keep all leaf print/update functions unchanged

### Phase C - textual routing later

Only later:

1. route one helper through `PresenterFacadePacket`
2. keep original helper callable as fallback

## Candidate exact boundaries

### Best first integration boundary

`UpdateFrameEndOverlays()` is the safest first runtime boundary because it
already centralizes:

- driving HUD
- realtime FPS
- work RAM usage
- low-work overlay

This gives the first patch a narrow behavioral surface.

### Second integration boundary

`PresentFrameHudAndTelemetry(...)` is the second safest boundary because it
already centralizes:

- frame presentation snapshot
- SH2 telemetry presentation
- segment overlap diagnostics

But this boundary is slightly broader than `UpdateFrameEndOverlays()`.

## Suggested first patch order

1. integrate decision-only assembly in `UpdateFrameEndOverlays()`
2. validate emulator stability
3. replace only local presence checks there
4. validate emulator stability again
5. only then consider `PresentFrameHudAndTelemetry(...)`

## Validation requirements

Every runtime patch in this sequence must pass:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

Invariant:

- ISO must remain exactly `4134912`

## Abort conditions

Stop and revert the patch if any of the following happens:

- emulator boot regression
- ISO growth beyond `4134912`
- changed HUD/overlay print ordering
- changed audio/frame pacing behavior
- new invalid opcode or silent close on emulator startup
