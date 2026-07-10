# SH2 Track Render Presentation Boundary Consolidated

## Objective

Record the current consolidated passive state of the SH2/track-render
presentation boundary.

This document is inventory-only.

It does not authorize runtime ownership changes by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the critical runtime host
- final HUD/debug presentation ordering remains unchanged
- no live ownership migration is introduced by this boundary

## Consolidated boundary

The SH2/track-render presentation subflow is split into four passive helper
families:

### 1. Track-render telemetry/producers

Files:

- `src/game_loop_track_render_telemetry_view_contracts.hpp`
- `src/game_loop_track_render_telemetry_view_assembler.hpp`
- `src/game_loop_track_render_producer_hint_contracts.hpp`
- `src/game_loop_track_render_producer_hint_assembler.hpp`

Role:

- define narrow track-render telemetry views
- expose producer-state hints from track-render telemetry

### 2. SH2 split snapshot assembly

Files:

- `src/game_loop_runtime_state.hpp`
- `src/game_loop_presentation_ops.hpp`

Role:

- assemble `Sh2SplitTelemetrySnapshot`
- bridge scheduler-side telemetry and track-render telemetry into a passive SH2
  snapshot

### 3. SH2 presentation packet assembly

Files:

- `src/game_loop_track_render_sh2_presentation_contracts.hpp`
- `src/game_loop_track_render_sh2_presentation_assembler.hpp`
- `src/game_loop_track_render_sh2_presentation_runtime_assembler.hpp`

Role:

- define `TrackRenderSh2PresentationPacket`
- assemble the packet from:
  - `Sh2SplitTelemetrySnapshot`
  - track-render telemetry view
  - dispatch/debug counters
- expose a compile-only bridge from lifecycle + track-render telemetry into the
  final SH2 presentation packet

### 4. Presentation output

Files:

- `src/game_loop_track_render_presentation_observability_presenter_ops.hpp`
- `src/game_loop_track_render_runtime_observability_ops.hpp`

Role:

- present `TrackRenderSh2PresentationPacket`
- keep host-local gating while delegating packet assembly to passive helpers

### 5. Compile-only presentation preview

Files:

- `src/game_loop_track_render_presentation_preview_contracts.hpp`
- `src/game_loop_track_render_presentation_preview_assembler.hpp`

Role:

- define one narrow compile-only preview directly above the current
  track-render telemetry + producer-hint + SH2-presentation seam
- group:
  - `TrackRenderTelemetryViewPacket`
  - `TrackRenderProducerHintPacket`
  - `TrackRenderSh2PresentationPacket`
- keep one inspection point that spans the current live boundary without
  widening runtime ownership

## What still stays in the host

The following ownership remains local to `src/game_loop_system.hpp`:

- cadence and call-site timing of SH2/track-render debug presentation
- runtime gating around whether track-render telemetry is available
- final ordering relative to other HUD/debug output

One additional narrow live cleanup is now active in the same host boundary:

- `FinishFrame()` builds `TrackRenderTelemetryViewPacket` once for the frame
- `PresentFrameHudAndTelemetry(...)` forwards that packet into
  `PrintSegmentOverlapDiagnostics(...)`
- the overlay path no longer rebuilds the same track-render telemetry packet
  locally
- `RenderTrackFrame(...)` now captures `submittedTrackFaces` once from the live
  `TrackRenderPacket`
- `BuildFramePresentationSnapshot(...)` no longer rebuilds another
  `TrackRenderPacket` just to recover the same face count
- the host now caches the `producerJobInFlight` hint once per frame before the
  first local consumer
- `TryDispatchSimulationOnSlave(...)` and `ScheduleCarPrepareIfEnabled(...)`
  now share that same per-frame hint instead of rebuilding it twice

## Functional coverage

The consolidated passive boundary now covers:

- track-render telemetry -> telemetry view
- telemetry view -> producer hint
- scheduler lifecycle packet + track-render telemetry -> SH2 split snapshot
- SH2 split snapshot + track-render telemetry -> SH2 presentation packet
- telemetry view + producer hint + SH2 presentation -> preview packet
- SH2 presentation packet -> debug/presenter output

## Why this boundary is considered consolidated

It now has:

- explicit telemetry/producers views
- explicit SH2 snapshot assembly
- explicit SH2 presentation packet assembly
- explicit compile-only lifecycle bridge
- explicit compile-only preview above the current live boundary
- host-local ownership preserved
- no live runtime ownership migration

## Recommended next moves

Do next:

1. keep this boundary runtime-stable
2. use it as precedent for other narrow track-render observability cuts
3. avoid broad presenter/runtime merges until a single-call-site retry is justified

Recent retry note:

- a local end-of-frame live retry using `TrackRenderPresentationPreviewPacket`
  increased the ISO envelope by `+2048`
- that retry was rolled back immediately
- the accepted live boundary remains the narrower telemetry reuse, face-count
  reuse, and per-frame producer-hint cache cuts only

Do not do next:

- move final SH2 presentation ownership off the host in one patch
- mix this boundary with audio/bootstrap changes
- widen this path into a broad render/debug live aggregate immediately
