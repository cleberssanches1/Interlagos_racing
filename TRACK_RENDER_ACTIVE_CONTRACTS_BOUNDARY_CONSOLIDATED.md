# Track Render Active Contracts Boundary Consolidated

## Objective

Consolidate the currently active `track-render` contracts that still participate
in the live runtime path, without widening ownership or changing behavior.

This document is runtime-shape inventory only.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the host/runtime owner
- `TrackSystem` retains producer/sort ownership
- no `N-1` reuse behavior is introduced here

## Active contract families

The live `track-render` path now relies on four active contract groups.

### 1. Frame packet boundary

Files:

- `src/game_loop_track_render_packet.hpp`
- `src/track_render_contracts.hpp`
- `src/track_render_state_assembler.hpp`
- `src/track_render_telemetry_assembler.hpp`
- `src/track_render_transition_ops.hpp`

Role:

- hold the frame-local track-render state
- expose render packet and telemetry packet content
- keep producer/sort ownership inside the existing track runtime

Live posture:

- foundational runtime boundary
- not presentation-specific
- still owned by the host/track runtime

### 2. Narrow telemetry view boundary

Files:

- `src/game_loop_track_render_telemetry_view_contracts.hpp`
- `src/game_loop_track_render_telemetry_view_assembler.hpp`
- `src/game_loop_track_render_runtime_observability_ops.hpp`

Role:

- derive `TrackRenderTelemetryViewPacket`
- expose the smallest live read surface reused by HUD/debug consumers
- avoid rebuilding wider track-render structures at each presentation site

Live posture:

- active and reused in the host path
- frame-local only
- no persistent ownership

### 3. Narrow producer-hint boundary

Files:

- `src/game_loop_track_render_producer_hint_contracts.hpp`
- `src/game_loop_track_render_runtime_observability_ops.hpp`

Role:

- derive `TrackRenderProducerHintPacket`
- carry the minimal in-flight producer hint needed by local host decisions

Live posture:

- active
- now assembled directly inside the runtime consumer helper
- no standalone wrapper remains

### 4. SH2 presentation boundary

Files:

- `src/game_loop_runtime_state.hpp`
- `src/game_loop_presentation_ops.hpp`
- `src/game_loop_track_render_sh2_presentation_contracts.hpp`
- `src/game_loop_track_render_sh2_presentation_assembler.hpp`
- `src/game_loop_track_render_presentation_observability_presenter_ops.hpp`
- `src/game_loop_track_render_runtime_observability_ops.hpp`

Role:

- derive `Sh2SplitTelemetrySnapshot`
- derive `TrackRenderSh2PresentationPacket`
- present SH2/producer telemetry in the host HUD/debug path

Live posture:

- active at the host call site
- packet assembly is now direct from the active runtime helper
- no intermediate runtime bridge leaf remains

### 5. Render-debug bundle boundary

Files:

- `src/game_loop_track_render_debug_contracts.hpp`
- `src/game_loop_render_debug_contracts.hpp`
- `src/game_loop_render_debug_assembler.hpp`

Role:

- derive `TrackRenderDebugPacket`
- join track/car debug packet slices into `RenderFrameDebugBundle`

Live posture:

- active derived-debug boundary
- packet assembly is now direct in the bundle consumer
- no standalone track debug wrapper remains

## Current live call shape

The active call graph is now intentionally flatter:

1. host obtains track-render telemetry once
2. host derives `TrackRenderTelemetryViewPacket`
3. host derives `TrackRenderProducerHintPacket` only when needed
4. host derives `Sh2SplitTelemetrySnapshot`
5. host derives `TrackRenderSh2PresentationPacket`
6. host presents SH2/track-render telemetry
7. render-debug path derives `TrackRenderDebugPacket` only inside the local
   render debug bundle flow

## What was removed from this boundary

These former wrappers/bridge leaves no longer exist:

- `src/game_loop_track_render_sh2_presentation_runtime_assembler.hpp`
- `src/game_loop_track_render_debug_assembler.hpp`
- `src/game_loop_track_render_producer_hint_assembler.hpp`

The runtime path now depends on the active contracts directly instead of
crossing one-file forwarding layers.

## What still stays outside

The following concerns remain intentionally outside this consolidated active
boundary:

- producer/sort ownership
- `TrackSystem` scheduling policy
- safe-mode policy decisions
- frame submission ordering
- broader presentation aggregate retries
- any `N-1` reuse strategy

## Why this boundary is now cleaner

The current shape reduces:

- one-hop wrapper assembly for producer hints
- one-hop wrapper assembly for SH2 presentation packet forwarding
- one-hop wrapper assembly for track debug packet derivation

while preserving:

- host-local ownership
- frame-local packet lifetime
- reversible call sites
- stable binary size and emulator behavior

## Recommended next move

Do next:

1. keep this active boundary stable
2. avoid removing deeper live packet layers unless they become pure forwarding
3. prefer future work on higher-level presentation consolidation or another
   subsystem boundary

Do not do next:

- fold `TrackRenderFramePacket` straight into the host presentation path
- merge scheduler/reuse concerns into this boundary
- widen this into a large render/debug aggregate in one patch
