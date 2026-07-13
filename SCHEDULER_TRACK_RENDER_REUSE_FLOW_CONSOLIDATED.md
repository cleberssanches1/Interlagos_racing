# Scheduler / Track-Render / Reuse Flow Consolidated

## Objective

Record one cross-boundary view of the passive and narrow-live groundwork that
now exists between:

- scheduler lifecycle observability
- SH2 / track-render presentation
- frame-reuse source staging
- reuse observability
- higher scheduler/reuse aggregate previews

This document is inventory-only.

It does not authorize runtime ownership moves by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the critical runtime host
- final SH2/HUD/debug print ordering remains unchanged
- scheduler dispatch, drain, producer, and reuse policy ownership remain local

## Covered subflows

This consolidated view spans the already documented boundaries:

- `SIMULATION_SCHEDULER_LIFECYCLE_BOUNDARY_CONSOLIDATED.md`
- `SH2_TRACK_RENDER_PRESENTATION_BOUNDARY_CONSOLIDATED.md`
- `FRAME_REUSE_OBSERVABILITY_BOUNDARY_CONSOLIDATED.md`
- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`

## Cross-boundary hierarchy

The current hierarchy across this combined flow is:

### Level 0 - scheduler lifecycle

Primary packet:

- `SimulationSchedulerLifecycleObservabilityPacket`

Purpose:

- group drain view, completion view, and scheduler telemetry view into one
  scheduler-side passive lifecycle boundary

Main files:

- `src/game_loop_simulation_scheduler_lifecycle_observability_contracts.hpp`
- `src/game_loop_simulation_scheduler_lifecycle_observability_assembler.hpp`
- `src/game_loop_simulation_scheduler_lifecycle_runtime_assembler.hpp`

### Level 1 - track-render SH2 presentation

Primary packets:

- `TrackRenderTelemetryViewPacket`
- `TrackRenderProducerHintPacket`
- `TrackRenderSh2PresentationPacket`

Purpose:

- narrow track-render telemetry and producer state
- bridge scheduler-side SH2 telemetry plus track-render state into one SH2
  presentation packet

Main files:

- `src/game_loop_track_render_telemetry_view_contracts.hpp`
- `src/game_loop_track_render_producer_hint_contracts.hpp`
- `src/game_loop_track_render_sh2_presentation_contracts.hpp`
- former bridge leaf `src/game_loop_track_render_sh2_presentation_runtime_assembler.hpp`
  later removed after its only consumer absorbed the forwarding logic directly
- `src/game_loop_track_render_runtime_observability_ops.hpp`

### Level 2 - scheduler lifecycle + track-render preview

Primary packet:

- `SchedulerTrackRenderPreviewPacket`

Purpose:

- group lifecycle state, track-render telemetry, and final SH2 presentation
  output into one compile-only preview above the current live track-render seam

Main files:

- preview-contract layer removed after smoke validation stopped depending on it
- preview assembler removed after smoke validation stopped depending on it

### Level 3 - frame-reuse source staging

Primary packets:

- `FrameReuseRuntimeOwnerPacket`
- `ReuseObservabilitySourceSnapshot`
- `ReuseObservabilitySourceOwnerPacket`
- `ReuseObservabilitySourcePacket`

Purpose:

- keep future real reuse source ownership outside `src/game_loop_system.hpp`
- stage and narrow raw reuse runtime state before reuse observability assembly

Main files:

- `src/frame_reuse_runtime_owner_contracts.hpp`
- `src/frame_reuse_runtime_owner_assembler.hpp`
- `src/frame_reuse_observability_source_contracts.hpp`
- `src/frame_reuse_observability_source_owner_contracts.hpp`
- `src/game_loop_reuse_source_state_contracts.hpp`
- `src/frame_reuse_observability_capture_ops.hpp`

### Level 4 - reuse observability

Primary packets:

- `ReuseObservabilityPacket`
- `ReuseObservabilityDebugBundle`
- `ReuseRuntimePreviewPacket`

Purpose:

- aggregate narrow reuse views
- prepare the local debug/presenter path
- keep one compile-only preview directly above raw runtime-owner staging

Main files:

- `src/game_loop_reuse_observability_contracts.hpp`
- `src/game_loop_reuse_runtime_packet_assembler.hpp`
- `src/game_loop_reuse_runtime_debug_bundle_assembler.hpp`
- the former `src/game_loop_reuse_runtime_preview_contracts.hpp` leaf was
  removed after the path was reduced to owner/source/observability helpers

### Level 5 - scheduler/reuse aggregate

Primary packets:

- `SchedulerReuseObservabilityPacket`
- `SchedulerReuseFlowObservabilityPacket`
- `SchedulerReusePreviewPacket`

Purpose:

- join scheduler telemetry, producer flags, lifecycle state, and reuse bundle
- expose the current highest compile-only aggregate on this axis

Main files:

- `src/game_loop_scheduler_reuse_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_flow_observability_contracts.hpp`
- former bridge leaf `src/game_loop_scheduler_reuse_observability_assembly_ops.hpp`
  later removed after smoke validation stopped depending on it

## Live vs passive state

### Narrow live cuts already active

- local `SimulationSchedulerTelemetryViewPacket` assembly
- local `TrackRenderTelemetryViewPacket` assembly
- local `TrackRenderProducerHintPacket` assembly
- local `TrackRenderSh2PresentationPacket` consumption

### Passive / compile-only layers above them

- `SchedulerTrackRenderPreviewPacket`
- `TrackReuseObservabilityPacket`
- `TrackReusePreviewPacket`
- `PresentTrackReusePreviewPacket(...)`
- `BuildTrackReuseRuntimeObservabilityPacket(...)`
- `BuildTrackReuseRuntimePreviewPacket(...)`
- `PresentTrackReuseRuntimePreviewPacket(...)`
- `FrameReuseRuntimeOwnerPacket` staging family
- `ReuseObservabilityPacket`
- `ReuseObservabilityDebugBundle`
- `ReuseRuntimePreviewPacket`
- `SchedulerReuseObservabilityPacket`
- `SchedulerReuseFlowObservabilityPacket`
- `SchedulerReusePreviewPacket`

### Additional low live cut now active

- one track-only `FrameReuseRuntimeOwnerPacket` path is now live at the local
  reuse seam in `src/game_loop_system.hpp`
- one narrow `TrackReuseRuntimeState` now feeds that owner packet on demand
- simulation-side reuse remains passive above that seam

## What still stays in the host

The following ownership still remains local to `src/game_loop_system.hpp`:

- simulation dispatch ordering
- drain/wait policy decisions
- track-render telemetry gating
- SH2 debug presentation cadence
- local call to `TryBuildReuseObservabilityDebugBundle(...)`
- final decision of when reuse observability is presented
- final ordering relative to HUD/overlay/debug output

## Why this consolidated map matters

This combined view makes the remaining work clearer:

- the lifecycle side is already structured
- the track-render side already has a narrow live seam
- the reuse side already has passive staging and aggregation
- the cross-boundary previews now show where higher-level consumers can attach
  later without recomposing the hierarchy in the host

## Recommended next moves

Do next:

1. keep this combined axis runtime-stable
2. keep future live retry anchored at `ReuseObservabilityPacket`
3. only consume higher aggregates after a stable lower-boundary live retry
4. use the preview packets as documentation and compile-only guard rails first

For the track-reuse branch specifically, the accepted low live point is now:

- one real track-only runtime-owner capture at the existing local reuse seam

The next preferred point above it is:

- `TrackReuseObservabilityPacket`
- `TrackReusePreviewPacket`

This keeps the already accepted track-only retry below the broader cross-domain
reuse aggregate.

The corresponding first live retry plan for that branch is:

- `TRACK_REUSE_OBSERVABILITY_FIRST_LIVE_RETRY_PLAN.md`
- `TRACK_REUSE_OBSERVABILITY_FIRST_LIVE_RETRY_PATCH_PLAN.md`

The compile-only bridge aligned to the same host seam remains:

- `BuildTrackReuseRuntimeObservabilityPacket(...)`
- `BuildTrackReuseRuntimePreviewPacket(...)`
- `PresentTrackReuseRuntimePreviewPacket(...)`

Do not do next:

- consume `SchedulerReuseFlowObservabilityPacket` live before `ReuseObservabilityPacket`
- move scheduler or producer ownership out of the host in one patch
- mix this axis with audio/bootstrap/car runtime work
- widen this axis into a broad presenter/runtime aggregate immediately

## Related documents

- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`
- `SIMULATION_SCHEDULER_LIFECYCLE_BOUNDARY_CONSOLIDATED.md`
- `SH2_TRACK_RENDER_PRESENTATION_BOUNDARY_CONSOLIDATED.md`
- `FRAME_REUSE_OBSERVABILITY_BOUNDARY_CONSOLIDATED.md`
- `GAME_LOOP_OBSERVABILITY_FLOW_PLAN.md`
