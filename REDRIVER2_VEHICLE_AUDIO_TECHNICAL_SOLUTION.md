# REDRIVER2 Vehicle Audio Technical Solution

## Goal

Extract the useful ideas from `REDRIVER2-master` and turn them into a practical Saturn implementation plan for `Interlagos_racing`.

This document is based on:

- Generated extraction report: [tools/reports/redriver2_vehicle_audio/redriver2_vehicle_audio_report.md](c:/saturn/SaturnRingLib-main/Projects/Interlagos_racing/tools/reports/redriver2_vehicle_audio/redriver2_vehicle_audio_report.md)
- Extraction script: [tools/extract_redriver2_vehicle_audio_logic.py](c:/saturn/SaturnRingLib-main/Projects/Interlagos_racing/tools/extract_redriver2_vehicle_audio_logic.py)

## What Driver 2 Actually Does

Driver 2 does not hard-switch one single engine loop based only on throttle.

It uses a layered model:

- `wheel_speed`, `gear`, `revs`, `changingGear` live in drivetrain state in `HANDLING_DATA`.
- `revsvol` and `idlevol` live in per-player audio state in `PLAYER`.
- `GetEngineRevs()` maps wheel speed plus thrust to a rev target through gear bands.
- `ControlCarRevs()` slews revs with capped rise and capped drop.
- When rev drop is too abrupt, it marks `changingGear`.
- Engine playback is two persistent layers:
  - one rev layer
  - one idle layer
- Tyre sounds are independent:
  - skid loop
  - wheel-surface loop

The key point is that Driver 2 solves continuity with volume blending, not by repeatedly stopping and restarting a single engine sample.

## Core Findings To Reuse

### 1. Revs must be derived from movement, not just input

Relevant source:

- `GetEngineRevs()` in `gamesnd.c`
- `HANDLING_DATA` in `dr2types.h`

Important behavior:

- Forward motion uses wheel speed bands per gear.
- Reverse motion forces gear `0`.
- Acceleration and non-acceleration use different ratios.
- The engine sound follows traction and wheel speed, not just button press.

Implication for Saturn:

- Our `debugEngineRpm` should remain the primary input.
- When `debugEngineRpm` is unavailable or unstable, fallback must use signed speed plus current gear, not only absolute speed.

### 2. Revs must be smoothed every frame

Relevant source:

- `ControlCarRevs()` in `gamesnd.c`

Important behavior:

- `maxrevrise` caps how fast RPM can rise.
- `maxrevdrop` caps how fast RPM can drop.
- Large rev drops set `changingGear = 1`.
- Airborne/wheelspin states force a different rev target.

Implication for Saturn:

- We need a dedicated audio RPM state:
  - `audioRpmCurrent`
  - `audioRpmTarget`
  - `changingGear`
- Audio RPM must not snap directly to physics RPM.
- The shift sound should be driven by gear delta and/or a large RPM drop event.

### 3. Idle and rev must be separate layers

Relevant source:

- Engine channel update in `gamesnd.c`

Important behavior:

- Driver 2 updates one channel with `revsvol`.
- It updates another channel with `idlevol`.
- At low RPM / no acceleration, idle comes up while rev bed goes down.
- Under acceleration, idle fades down and rev bed fades up.

Implication for Saturn:

- Our current one-channel engine design is the wrong topology for stable behavior.
- The Saturn version should use:
  - `Channel 0`: idle bed
  - `Channel 1`: rev bed
  - `Channel 2`: shift one-shot
  - `Channel 3`: tyre loop
- Crowd ambience must be removed from PCM or moved elsewhere, because the fourth PCM channel is needed for tyres.

### 4. Skid and wheel/road sound are separate from engine

Relevant source:

- `handling.c`
- `wheelforces.c`

Important behavior:

- Driver 2 chooses skid sound from slip conditions.
- It chooses wheel noise from surface type plus speed.
- Both can restart independently from engine audio.

Implication for Saturn:

- `TILDLQ` should stay isolated from engine logic.
- Reverse, braking and wheelspin should never stop engine audio.
- Tyre sound must be controlled by slip/brake/surface state only.

## Correct Behavior Matrix For Interlagos

This is the target behavior for our project.

### Standstill and neutral

- Condition:
  - speed near zero
  - no throttle
  - no braking skid
- Sound:
  - `EIDLLQ` audible
  - `EGLOLQ` muted
  - `EGHILQ` muted

### Forward launch and low RPM acceleration

- Condition:
  - forward gear selected
  - throttle on
  - RPM below high band
- Sound:
  - `EIDLLQ` fading down
  - `EGLOLQ` fading up

### High RPM before upshift

- Condition:
  - throttle on
  - RPM above high crossover
- Sound:
  - `EGLOLQ` fading down
  - `EGHILQ` fading up

### Upshift

- Condition:
  - gear increases
  - or RPM drop exceeds shift threshold while accelerating
- Sound:
  - play `SUPLQ`
  - do not stop engine layers
  - briefly bias toward `EGLOLQ` after the shift if RPM falls back into low band

### Downshift

- Condition:
  - gear decreases
- Sound:
  - play `SDNLQ`
  - keep engine layers continuous

### Reverse engage

- Condition:
  - gear changes from forward/neutral to reverse
- Sound:
  - play `SDNLQ`
  - engine remains active

### Reverse acceleration

- Condition:
  - reverse selected
  - throttle on
- Sound:
  - use `EGLOLQ` as primary acceleration bed
  - `EIDLLQ` fades down while throttle is applied
  - `EGHILQ` is optional in reverse and should only appear if reverse RPM can actually reach the high crossover

### Return to standstill after reverse or forward motion

- Condition:
  - speed near zero again
  - throttle released
- Sound:
  - `EGLOLQ` and `EGHILQ` fade out
  - `EIDLLQ` fades back in
  - engine session stays alive; it must not fall silent

### Braking skid

- Condition:
  - slip/brake condition true
- Sound:
  - `TILDLQ` loop on tyre channel
  - engine layers continue independently

## Recommended Saturn Architecture

### Audio state

Add a dedicated runtime state separate from raw physics:

```cpp
struct EngineAudioState
{
    int16_t audioRpmCurrent;
    int16_t audioRpmTarget;
    int8_t currentGear;
    int8_t previousGear;
    bool changingGear;
    bool reverseActive;
    uint8_t idleVolume;
    uint8_t lowVolume;
    uint8_t highVolume;
    uint8_t activeRevBand;
};
```

### Channel allocation

- `0`: `EIDLLQ` loop
- `1`: rev loop bed (`EGLOLQ` or `EGHILQ`)
- `2`: `SUPLQ` / `SDNLQ`
- `3`: `TILDLQ`

Do not keep crowd on PCM while vehicle audio is being stabilized.

### RPM and gear pipeline

Per frame:

1. Read signed speed, selected gear, throttle, brake, wheelspin, grounded state.
2. Build `audioRpmTarget` from:
   - physics RPM if reliable
   - otherwise signed speed + gear table
3. If airborne or spinning, allow elevated RPM target.
4. Slew `audioRpmCurrent` toward target using bounded rise/drop.
5. If drop exceeds threshold, set `changingGear`.

### Band selection

- `idle` band:
  - stopped or almost stopped
  - no throttle
- `low` band:
  - moving or throttling
  - RPM below high threshold
- `high` band:
  - RPM above high threshold

This selection must be based on `audioRpmCurrent`, not raw button state alone.

### Volume model

Use crossfade, not hard switch:

- idle state:
  - idle high
  - rev low
- low accel:
  - idle medium/low
  - low high
- high accel:
  - idle very low
  - high high

The rev channel can swap between `EGLOLQ` and `EGHILQ`, but only when the crossfade target changes band. The idle channel must continue independently.

### Shift logic

Trigger `SUPLQ` / `SDNLQ` on:

- explicit gear delta
- reverse engage
- reverse exit

Do not require race phase `Running` for shift one-shots.

### Reverse policy

Reverse must not mute the engine.

Rules:

- reverse selected + throttle:
  - idle fades down
  - low rev fades up
- reverse selected + no throttle + stop:
  - idle fades back in
- reverse is just another drivetrain mode; it is not an audio shutdown state

## Why The Current Saturn Attempt Fails

The current implementation uses one engine PCM channel and tries to emulate all behavior by restarting loops and changing pitch. That causes three structural problems:

- loop restart replays sample attack and creates stutter
- idle/low/high transitions compete for the same channel
- reverse and gear-change logic can accidentally leave the engine with no active bed

This is a topology problem, not just a threshold problem.

## Second-Step Implementation Plan

### Step 1

Replace the current single-engine-channel design with dual persistent engine layers:

- idle loop channel
- rev loop channel

### Step 2

Add `audioRpmCurrent` smoothing with capped rise/drop and a `changingGear` flag.

### Step 3

Drive `SUPLQ` and `SDNLQ` from gear deltas, including reverse engage/disengage.

### Step 4

Move `EGLOLQ`/`EGHILQ` selection to a rev-band crossfade model.

### Step 5

Keep `TILDLQ` as a separate tyre loop driven by slip and brake state.

### Step 6

Add a debug overlay for:

- selected gear
- signed speed
- physics RPM
- audio RPM
- current rev band
- idle/rev volumes
- shift event

## Script Usage

Regenerate the extraction report with:

```powershell
python tools\extract_redriver2_vehicle_audio_logic.py
```

Outputs:

- `tools/reports/redriver2_vehicle_audio/redriver2_vehicle_audio_report.md`
- `tools/reports/redriver2_vehicle_audio/redriver2_vehicle_audio_report.json`
