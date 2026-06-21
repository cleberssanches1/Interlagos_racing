# Game Loop Presenter Runtime Alternatives

## Objective

Record the first failed runtime presenter boundary, preserve the rollback
decision, and define the next safe runtime alternatives without reopening the
same emulator regression.

## Current status

The passive presenter/facade groundwork remains valid and stable.

What failed was the first live runtime hookup attempt inside
`src/game_loop_system.hpp:2077`.

Current stable state:

- `src/game_loop_system.hpp:2077` `UpdateFrameEndOverlays()` directly calls
  `PrintDrivingHud()`
- `src/game_loop_system.hpp:2110` `PrintDrivingHud()` owns its current local
  drivetrain snapshot assembly
- no presenter passive packet is consumed in live runtime
- stable build remains at ISO `4134912`

## Failed runtime boundary

### Boundary

`src/game_loop_system.hpp:2077` `UpdateFrameEndOverlays()`

### Attempted direction

Use presenter passive/facade packets to mirror or replace local end-of-frame HUD
presence checks.

### Observed result

The build passed, but emulator startup/runtime regressed and produced invalid
opcode failures.

### Conclusion

`UpdateFrameEndOverlays()` is no longer the recommended first runtime presenter
boundary.

Do not retry it first.

## Root constraint

For the next runtime move, emulator stability is more important than advancing
presenter ownership.

That means:

- no new live presenter routing inside the existing end-of-frame HUD path first
- no runtime patch that increases ISO beyond `4134912`
- no runtime patch that changes print/update ordering
- no runtime patch that mixes with audio, scheduler, or simulation work

## Alternative runtime boundaries

### Option 1 - `PresentFrameHudAndTelemetry(...)` decision-only mirror

Candidate:

- `src/game_loop_system.hpp:2031` `PresentFrameHudAndTelemetry(...)`

Why it is safer than retrying `UpdateFrameEndOverlays()`:

- this path already centralizes frame presentation decisions
- it is less tied to the always-on driving HUD path
- it allows decision-only mirroring on already assembled frame data

Strict limit:

- decision-only use of passive presenter/facade packets
- no movement of `SRL::Debug::Print(...)` ownership
- no movement of FPS, work RAM, or low-work overlay ownership

Remove-first target:

- only replace an equivalent local presence branch if the original branch is
  removed in the same patch

### Option 2 - off-path compile-only assembly

Candidate:

- compile-only validation inside a non-executed local helper or validation-only
  code path

Why it is safe:

- exercises include/assembly shape
- does not alter live emulator behavior

Why it is limited:

- gives structural confidence only
- does not prove runtime suitability

### Option 3 - very narrow helper-local bridge

Candidate:

- `src/game_loop_system.hpp:2131`
  `PrintSegmentOverlapDiagnostics(...)`

Why it may be viable later:

- narrower scope than the full overlay/HUD end-of-frame path
- easier to remove-first if a single equivalent presence check exists

Why it is not first:

- still touches active presentation behavior
- lower value than documenting and preparing the broader decision-only boundary

## Recommended next order

1. keep runtime unchanged
2. preserve `UpdateFrameEndOverlays()` as direct owner
3. use passive docs/inventory to mark that boundary as failed-first
4. if runtime integration is retried, start with
   `PresentFrameHudAndTelemetry(...)`
5. keep the patch decision-only and substitutional
6. revert immediately if emulator behavior changes

For the exact narrow-packet retry order, see:

- `GAME_LOOP_PRESENTER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

## Patch checklist for the next live attempt

The next runtime presenter patch must satisfy all of the following:

- touches one boundary only
- adds no persistent presenter member
- adds no heap allocation
- adds no cross-frame cache
- removes equivalent host-local branching in the same patch
- preserves call ordering exactly
- preserves text formatting exactly
- preserves ISO `4134912`

## Validation ritual

Every attempt must pass:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

Runtime acceptance:

- emulator boots
- HUD/overlay ordering matches previous stable build
- no invalid opcode
- no silent close on startup

## Abort conditions

Revert immediately if any of these occur:

- emulator startup regression
- invalid opcode
- silent close on emulator open
- ISO grows above `4134912`
- HUD/overlay ordering changes
- sound/frame pacing behavior changes
