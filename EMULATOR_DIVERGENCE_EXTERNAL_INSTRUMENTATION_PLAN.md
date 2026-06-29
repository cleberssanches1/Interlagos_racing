# Emulator Divergence External Instrumentation Plan

## Objective

Document a safe external-instrumentation path to investigate why a build can
remain stable at ISO `4134912` and still diverge at emulator startup/runtime.

This document is investigation-only.

It does not authorize new live runtime substitutions in critical files.

## Problem statement

Recent `scheduler/reuse` and adjacent observability retries have shown the
same pattern:

- passive headers compile
- stable Saturn build validation passes
- ISO remains exactly `4134912`
- emulator behavior can still regress with:
  - invalid opcode
  - silent close
  - startup failure
  - runtime divergence after boot

That means ISO-size preservation and successful link are necessary, but not
sufficient, for this boundary.

## Investigation principle

When a boundary is this sensitive, the next move should not be another live
retry inside `src/game_loop_system.hpp`.

The safe next step is:

1. keep the runtime at the last known emulator-stable revision
2. add external observability outside the game binary
3. capture emulator-side evidence around the failing startup/runtime window
4. use that evidence to decide whether the real failure is:
   - timing-sensitive
   - memory-layout-sensitive
   - SH2 execution-order-sensitive
   - emulator-specific

## Scope

This plan focuses on external evidence for:

- Master SH2 / Slave SH2 execution slices
- interrupt timing
- SCU / DMA activity
- VDP1 / VDP2 frame boundaries
- input/frame progression
- optional watchpoints for game state addresses

This plan explicitly avoids:

- new runtime aggregation in `src/game_loop_system.hpp`
- new ownership movement in scheduler/reuse code
- new `printf`-style hot-path diagnostics inside the game

## Current stable anchor

Use this exact anchor before collecting evidence:

- game runtime kept at the last emulator-stable state
- `SimulationSchedulerTelemetryViewPacket` live
- `TrackRenderProducerStatePacket` live
- no live `ReuseObservabilityPacket`
- no extra retry/deep decision packet in the telemetry path
- ISO exactly `4134912`

## External instrumentation stack

### Layer 1 - stable build gate

Keep using:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

Purpose:

- prove the binary envelope did not drift before emulator-side capture

### Layer 2 - Kronos trace hooks

Use the existing Kronos-oriented materials already present in the repo:

- `kronos_ai_trace_plan.md`
- `kronos_trace_bridge_spec.md`
- `kronos_trace_hook_map.md`
- `kronos_watch_quick_test.md`

Purpose:

- observe emulator-side execution without perturbing the game binary

### Layer 3 - targeted watch configuration

Prefer watchpoints only for state that helps correlate startup/runtime
divergence:

- frame counter
- simulation dispatch/completion state
- track producer in-flight state
- critical camera/car state only if the fault occurs after boot

Avoid broad RAM dumps first.

## Minimal evidence pipeline

### Step 1 - lock the known-good game build

Before any emulator investigation run:

1. validate the stable build
2. record ISO size
3. do not change `src/game_loop_system.hpp`
4. do not combine investigation with new runtime patches

Acceptance:

- ISO `4134912`
- emulator-stable baseline still available

### Step 2 - capture frame boundary evidence

Instrument only:

- `FRAME_BEGIN`
- `FRAME_END`
- `VDP2_VBLANK_IN`
- `VDP2_VBLANK_OUT`

Question answered:

- does failure happen before normal frame cadence starts, or after cadence
  begins?

### Step 3 - capture SH2 scheduling evidence

Add or enable:

- `SH2_EXEC_SLICE`
- `SH2_INTERRUPT`
- `SCU_INTERRUPT`

Question answered:

- does the failing build diverge because the Master/Slave execution sequence
  changes before the crash?

### Step 4 - capture producer/scheduler-adjacent evidence

Add or enable:

- `SCU_DMA`
- `SH2_DMA`
- `VDP1_DRAW_BEGIN`
- `VDP1_DRAW_END`

Question answered:

- is the regression tied to rendering/producer activity rather than pure boot?

### Step 5 - add narrow watchpoints only if needed

If the fault reproduces after startup and the trace is still ambiguous, add a
small watch set for:

- frame counter
- simulation job-in-flight flag
- simulation completed flag
- track producer in-flight flag

Question answered:

- does emulator divergence correlate with state mutation ordering rather than
  raw CPU timing alone?

## Trigger windows

Use two investigation windows only:

### Window A - boot failure

Capture:

- from process start until first failure

Use when:

- game does not open
- emulator closes silently
- invalid opcode occurs before gameplay

### Window B - post-boot divergence

Capture:

- from stable startup through the first divergence trigger

Use when:

- game opens
- divergence appears only after gameplay starts

## Comparison protocol

For every future suspicious patch:

1. keep one known-good trace capture
2. capture one failing trace with the same emulator profile
3. compare in this order:
   - first `FRAME_BEGIN`
   - first `VBLANK_IN/OUT`
   - first SH2 slice divergence
   - first interrupt ordering divergence
   - first DMA/render divergence

The first mismatch is more valuable than the final crash site.

## Decision rules

### If divergence happens before normal frame cadence

Bias toward:

- startup ordering sensitivity
- interrupt timing sensitivity
- binary layout sensitivity in critical host code

Do not:

- expand runtime packetization further

### If divergence happens after frame cadence begins

Bias toward:

- producer/scheduler ordering
- render-trigger timing
- race-like behavior between Master and Slave windows

Do not:

- widen passive aggregate integration yet

### If traces match until very late and then diverge

Bias toward:

- memory aliasing / undefined behavior
- emulator-specific sensitivity
- stale state read/write ordering

## Recommended execution order

1. preserve current stable runtime
2. validate stable build
3. use `kronos_watch_quick_test.md`
4. capture one baseline trace
5. only then attempt one small runtime experiment
6. if emulator regresses, compare traces before touching runtime again

## Abort conditions

Stop the investigation pass and revert to baseline if:

- instrumentation changes the game binary
- instrumentation changes ISO size
- instrumentation forces new runtime ownership changes
- evidence capture becomes broader than needed for the first mismatch

## Expected outcome

This plan should let us answer, with evidence:

- whether emulator divergence starts before or after frame cadence
- whether the first mismatch is SH2 ordering, interrupt timing, DMA, or render
- whether a future live retry should target:
  - no runtime retry yet
  - a safer external comparison pass
  - a much narrower local retry in a different boundary

## Related documents

- `kronos_ai_trace_plan.md`
- `kronos_trace_bridge_spec.md`
- `kronos_trace_hook_map.md`
- `kronos_watch_quick_test.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PASSIVE_TO_RUNTIME_INTEGRATION_PLAN.md`
