# Track Render Presentation Boundary Consolidated

## Objective

Consolidate the current narrow live boundary where track-render telemetry is
consumed by presentation/debug code without moving producer/sort ownership.

## Boundary

This boundary is limited to the Master-side presentation path in
`src/game_loop_system.hpp`:

- `BuildFramePresentationSnapshot(...)`
- `PresentFrameHudAndTelemetry(...)`
- `PrintSh2SplitTelemetry(...)`

It does not change:

- `TrackSystem` producer ownership
- Slave producer/sort dispatch
- safe mode
- synchronous fallback
- render submission order

## Packet chain used

The boundary now relies on one narrow shared input:

- `src/game_loop_track_render_telemetry_view_contracts.hpp`

and derives the smaller producer-state view only when needed:

- `src/game_loop_track_render_producer_state_contracts.hpp`

One higher compile-only aggregation now also exists above that live boundary:

- `src/game_loop_track_render_presentation_observability_contracts.hpp`
- `src/game_loop_track_render_presentation_observability_assembler.hpp`

## Current live behavior

Within the same frame presentation path:

1. `TrackRenderTelemetryViewPacket` is assembled once
2. the same packet feeds `Sh2SplitTelemetrySnapshot`
3. the same packet optionally feeds `TrackRenderProducerStatePacket`
4. `PrintSh2SplitTelemetry(...)` consumes the derived producer state

This removes one repeated `BuildTrackRenderTelemetry(...)` pass from the
HUD/debug presentation path while keeping the runtime call graph local and
reversible.

One higher passive packet exists above this live boundary:

- `TrackRenderPresentationObservabilityPacket`

One compile-only presenter helper now also exists above that packet:

- `src/game_loop_track_render_presentation_observability_presenter_ops.hpp`
- `src/game_loop_track_render_sh2_presentation_contracts.hpp`
- `src/game_loop_track_render_sh2_presentation_assembler.hpp`

Current status:

- compile-only only
- not consumed by the runtime path
- kept ready for a future remove-first retry
- presenter formatting for the producer-state line is now already externalized
  off-path
- one compile-only packet now also exists for the full
  `PrintSh2SplitTelemetry(...)` presentation boundary

One narrow live use is now accepted below that packet level:

- `PresentTrackRenderProducerStatePacket(...)`
- `PresentTrackRenderSh2BusyLine(...)`
- `PresentTrackRenderSh2SimSafeLine(...)`
- `PresentTrackRenderSh2SimFallbackLine(...)`

This live use keeps the higher `TrackRenderPresentationObservabilityPacket`
compile-only.

`TrackRenderSh2PresentationPacket` remains prepared for this boundary, but is
not currently consumed by the runtime path.

## Why this cut is safe

- no persistent cache was introduced
- no new ownership was introduced
- no packet crosses frame boundaries
- the packet is consumed immediately in the same path
- producer-hint scheduling still uses its own narrow path

## What still stays outside

The following remain intentionally separate:

- `IsTrackProducerJobInFlightHint(...)`
- track producer scheduling
- `TrackRenderFramePacket`
- `TrackRenderDebugPacket`
- any `N-1` reuse decision

## Next safe step

The next safe Track Render move is to retry this packet only after repeated
stable emulator runs, and only as a remove-first local presentation/debug cut.

That means:

- keep `TrackRenderPresentationObservabilityPacket` local to the HUD/debug path
- avoid expanding it into producer ownership or scheduler ownership
- do not pull `TrackRenderFramePacket` into the live presentation site
- prefer one more substitutional debug/presentation read before any broader
  runtime move
