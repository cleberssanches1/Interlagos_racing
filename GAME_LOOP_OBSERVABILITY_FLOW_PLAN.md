# Game Loop Observability Flow Plan

## Objective

Prepare a future extraction of observability/debug presentation out of `src/game_loop_system.hpp` without touching the critical runtime yet.

## Current passive building blocks

### Presentation

- `src/game_loop_presentation_ops.hpp`
- `src/game_loop_presentation_debug_contracts.hpp`
- `src/game_loop_presentation_debug_assembler.hpp`

Provides passive builders for:

- `FramePresentationSnapshot`
- `Sh2SplitTelemetrySnapshot`
- `PresentationDebugBundle`
- `PresenterInputBundle`

### Overlay

- `src/game_loop_overlay_contracts.hpp`
- `src/game_loop_overlay_state_assembler.hpp`

Provides passive packets for:

- segment snapshot
- render window snapshot
- nearest segment snapshot
- overlay diagnostics
- overlay event transitions

### Telemetry

- `src/game_loop_telemetry_contracts.hpp`
- `src/game_loop_telemetry_state_assembler.hpp`

Provides passive packets for:

- face/shadow overlay
- ground probe overlay
- physics query overlay
- segment event text
- SH2 telemetry
- realtime FPS telemetry

### Aggregation

- `src/game_loop_observability_contracts.hpp`
- `src/game_loop_observability_state_assembler.hpp`

Provides passive aggregation for:

- `OverlayPacketFlow`
- `TelemetryPacketFlow`
- `MemoryPresentationPacketFlow`
- `FrameObservabilityPacket`

### Memory / debug presentation

- `src/game_loop_memory_presentation_contracts.hpp`
- `src/game_loop_memory_presentation_state_assembler.hpp`

Provides passive packets for:

- work RAM usage summary
- low-work overlay summary
- high-work trace summary
- low-work trace snapshots

Current local runtime coverage already integrated:

- `PrintWorkRamUsageRealtime()`
- light `UpdateLowWorkFreeOverlay()` presentation packets:
  - low-work header
  - low-work track/prefetch ticks
  - low-work breakdown
  - high-work summary
  - low-work tag group summary
  - low-work allocator summary
- memory trace packets assembled locally in:
  - `MaybeLogHighWorkRamTrace()`
  - `MaybeLogLowWorkRamTrace()`
- SH2/track-render presentation packet assembled locally in:
  - `PresentFrameHudAndTelemetry(...)`
  - consumed through `TrackRenderSh2PresentationPacket` instead of ad hoc
    producer-flag branching in `src/game_loop_system.hpp`
- track-render observability runtime helpers now live behind:
  - `src/game_loop_track_render_runtime_observability_ops.hpp`
  - this consolidates the local telemetry-view, producer-hint, and SH2
    presentation packet assembly without changing runtime ownership
- one compile-only preview now also exists directly above that local live seam:
  - `src/game_loop_track_render_presentation_preview_contracts.hpp`
  - `src/game_loop_track_render_presentation_preview_assembler.hpp`
  - it groups:
    - `TrackRenderTelemetryViewPacket`
    - `TrackRenderProducerHintPacket`
    - `TrackRenderSh2PresentationPacket`
- the SH2/track-render presentation subflow is now documented as a
  consolidated passive boundary in:
  - `SH2_TRACK_RENDER_PRESENTATION_BOUNDARY_CONSOLIDATED.md`
- the simulation scheduler lifecycle subflow is now documented as a
  consolidated passive boundary in:
  - `SIMULATION_SCHEDULER_LIFECYCLE_BOUNDARY_CONSOLIDATED.md`
- one compile-only preview now also exists directly above lifecycle +
  track-render reuse:
  - `src/game_loop_scheduler_track_render_preview_contracts.hpp`
  - `src/game_loop_scheduler_track_render_preview_assembler.hpp`
  - it groups:
    - `SimulationSchedulerLifecycleObservabilityPacket`
    - `TrackRenderTelemetryViewPacket`
    - `TrackRenderSh2PresentationPacket`
- the full combined map for scheduler lifecycle + track-render + reuse is now
  documented in:
  - `SCHEDULER_TRACK_RENDER_REUSE_FLOW_CONSOLIDATED.md`

Current scheduler/reuse status:

- `ReuseObservabilityPacket` and its debug bundle remain passive-only
- no live runtime producer/history state is wired through
  `src/game_loop_system.hpp` yet
- the next safe step there is still a local passive assembly point first, not a
  live retry
- that passive assembly point is now prepared in:
  - `src/game_loop_reuse_runtime_observability_ops.hpp`
  - `src/game_loop_reuse_runtime_owner_assembler.hpp`
  - `src/game_loop_reuse_runtime_debug_bridge_assembler.hpp`
  - it adapts raw `frame_reuse` runtime packets/telemetry into
    `ReuseObservabilityPacket` and `ReuseObservabilityDebugBundle`
  - host runtime ownership is still intentionally unchanged
- one level above that, the scheduler/reuse aggregate is now also prepared as a
  compile-only boundary in:
  - `src/game_loop_scheduler_reuse_observability_contracts.hpp`
  - `src/game_loop_scheduler_reuse_observability_assembler.hpp`
  - `src/game_loop_scheduler_reuse_flow_observability_contracts.hpp`
  - `src/game_loop_scheduler_reuse_flow_observability_assembler.hpp`
  - `src/game_loop_scheduler_reuse_observability_assembly_ops.hpp`
  - `src/game_loop_scheduler_reuse_debug_telemetry_contracts.hpp`
  - `src/game_loop_scheduler_reuse_debug_telemetry_assembler.hpp`
  - `src/game_loop_scheduler_reuse_runtime_debug_bridge_assembler.hpp`
  - `src/game_loop_scheduler_reuse_preview_contracts.hpp`
  - `src/game_loop_scheduler_reuse_preview_assembler.hpp`
  - it joins scheduler telemetry, explicit producer flags, and the future
    `frame_reuse` runtime owner path without reopening `src/game_loop_system.hpp`
  - and now exposes the narrow `SchedulerReuseDebugTelemetryPacket` marker above
    `SchedulerReuseFlowObservabilityPacket` for future presenter/debug consumers
  - plus one compile-only `SchedulerReusePreviewPacket` that groups the current
    flow packet with that debug marker
- `src/game_loop_system.hpp` now has a first local assembly point for this
  boundary:
  - `TryBuildReuseObservabilityDebugBundle(...)`
  - it is currently wired with null/default inputs only, so runtime behavior is
    unchanged while the host capture seam and assembly seam are established
- current repo status:
  - no live `SimulationFrameHistoryState`
  - no live `TrackFrameHistoryState`
  - no live `FrameReuseTelemetry`
  - so the host seam is now explicitly split into source-state capture first
    and packet assembly second, awaiting a future safe runtime source
- the future safe source is now prepared outside the critical loop as:
  - `src/frame_reuse_runtime_owner_contracts.hpp`
  - `src/frame_reuse_runtime_owner_assembler.hpp`
  - `src/frame_reuse_runtime_observability_source_assembler.hpp`
  - `src/frame_reuse_runtime_observability_owner_assembler.hpp`
  - `src/game_loop_reuse_runtime_preview_contracts.hpp`
  - `src/game_loop_reuse_runtime_preview_assembler.hpp`
  - `src/game_loop_reuse_runtime_source_assembler.hpp`
  - `src/game_loop_reuse_runtime_owner_assembler.hpp`
  - `src/game_loop_reuse_runtime_debug_bridge_assembler.hpp`
  - `src/frame_reuse_observability_source_contracts.hpp`
  - `src/frame_reuse_observability_source_assembler.hpp`
  - `src/frame_reuse_observability_source_owner_contracts.hpp`
  - `src/frame_reuse_observability_source_owner_assembler.hpp`
  - `src/frame_reuse_observability_capture_ops.hpp`
  - `src/game_loop_reuse_source_state_contracts.hpp`
  - `src/game_loop_reuse_source_state_assembler.hpp`
  - `src/game_loop_reuse_source_owner_contracts.hpp`
  - `src/game_loop_reuse_source_owner_assembler.hpp`
  - `src/game_loop_reuse_runtime_observability_contracts.hpp`
  - `src/game_loop_reuse_runtime_packet_assembler.hpp`
  - `src/game_loop_reuse_runtime_debug_bundle_assembler.hpp`
  - the runtime owner packet is currently prepared as passive-only compile-time
    surface
  - one compile-only `ReuseRuntimePreviewPacket` now groups that raw owner
    packet with the current `ReuseObservabilityDebugBundle`
  - the host still captures the frame-reuse-domain owner packet directly, then
    adapts it to the observability owner/source boundary

## Recommended future runtime fit

### Phase 1 - textual shadow path only

Keep `GameLoopSystem` as the only runtime owner, but let it build passive packets and still print using existing direct functions.

Target:

- no behavior change
- no new runtime ownership transfer
- no change in audio/HUD/frame pacing

### Phase 2 - local observability assembly point

Introduce one local method inside `GameLoopSystem` that assembles:

- overlay flow
- telemetry flow
- memory/debug flow

but still consumes them immediately in the same file.

Target:

- reduce ad hoc assembly spread
- keep stack/layout changes minimal
- make regression isolation easier

### Phase 3 - dedicated passive presenter facade

Only after repeated stable builds, introduce a passive presenter/facade that receives:

- `FrameObservabilityPacket`
- memory/debug packets
- presentation/HUD packets
- overlay/debug packets

and returns no runtime side effects except formatted print decisions.

Target:

- move formatting decisions away from the loop
- keep data ownership in `GameLoopSystem`

### Phase 4 - optional runtime extraction

Only after phases 1-3 are stable:

- extract a non-owning `GameLoopObservabilitySystem`
- keep all driver calls on Master SH2
- keep all packet creation deterministic and frame-local

## Current runtime call sites

The current observability/presentation flow is still driven directly by `GameLoopSystem`.

Primary path:

1. `FinishFrame()`
2. `BuildFramePresentationSnapshot()`
3. `PresentFrameHudAndTelemetry(...)`
4. `UpdateFrameEndOverlays()`

Current call-site ownership:

- `BuildFramePresentationSnapshot()`
  - builds the passive `FramePresentationSnapshot`
  - already uses `src/game_loop_presentation_ops.hpp`
- `PresentFrameHudAndTelemetry(...)`
  - prints segment/overlay diagnostics
  - prints SH2 split telemetry
- `UpdateFrameEndOverlays()`
  - prints drivetrain HUD
  - updates realtime FPS overlay
  - prints Work RAM usage
  - updates low-work memory overlay

Secondary local builders still inside `src/game_loop_system.hpp`:

- `BuildOverlayDiagnosticsSnapshot(...)`
- `BuildSegmentOverlaySnapshot(...)`
- `BuildSh2SplitTelemetrySnapshot()`
- `BuildExtendedDrivetrainOverlaySnapshot()`

## Safe future integration order

The next runtime step should happen in this exact order.

### Step 1 - local packet assembly only

Inside `src/game_loop_system.hpp`, introduce stack-local packet assembly points only.

No new persistent members.
No ownership transfer.
No presenter object yet.

Recommended local assembly sequence:

1. assemble overlay packets
2. assemble telemetry packets
3. assemble memory/debug packets
4. aggregate into one `FrameObservabilityPacket`
5. immediately consume with the existing print functions

### Step 2 - keep existing print calls

Do not replace printing logic in the same patch.

The first runtime integration must only:

- assemble passive packets;
- keep existing direct `SRL::Debug::Print(...)` call paths;
- avoid any new branching that can alter frame pacing.

### Step 3 - move formatting later

Only after stable emulator runs:

- route existing print helpers through the passive packets;
- then reduce the direct snapshot assembly spread inside `GameLoopSystem`.

## Exact future cut points

### Overlay flow

Best initial assembly point:

- inside `PrintSegmentOverlapDiagnostics(...)`

Reason:

- the function already centralizes segment, face, probe and event diagnostics;
- packet creation can remain frame-local;
- no scheduler/audio dependency is introduced.

### Telemetry flow

Best initial assembly points:

- `BuildSh2SplitTelemetrySnapshot()`
- `UpdateRealtimeFpsOverlay()`

Reason:

- SH2 telemetry and FPS telemetry are already separated from gameplay state mutation;
- both are naturally passive exports.

### Memory / debug flow

Best initial assembly points:

- `PrintWorkRamUsageRealtime()`
- `UpdateLowWorkFreeOverlay()`
- `MaybeLogHighWorkRamTrace()`
- `MaybeLogLowWorkRamTrace()`

Reason:

- these are already presentation/debug-only paths;
- they do not need to affect simulation, camera, input or audio.

Current local runtime use now also includes:

- one local `MemoryDebugPresentationBundle` assembly/consume step in
  `UpdateFrameEndOverlays()`
- ownership still remains inside `src/game_loop_system.hpp`

## Runtime guard rails for the next patch

When the first runtime integration happens, keep all of the following true:

- all new observability packets stay stack-local;
- `GameLoopSystem` gains no new long-lived observability state;
- no new arrays, vectors or heap allocations are introduced;
- no new cross-frame caching is added;
- no Master/Slave scheduling behavior changes in the same patch;
- no audio, HUD or drivetrain behavior changes in the same patch;
- validate clean build and stable ISO immediately after the change.

One narrow exception now exists and is accepted on the reuse axis:

- one local track-only `TrackReuseRuntimeState` was added in the host
- it feeds the already existing reuse debug seam only
- it does not widen scheduler, audio, or render ownership

## Hard constraints

- do not grow `src/game_loop_system.hpp` unless equivalent runtime code is removed
- do not add new member state to `GameLoopSystem` unless required
- do not mix observability extraction with scheduler, simulation, or audio changes
- validate every step with `tools/validate_saturn_stable_build.ps1`
- keep the ISO at `4134912`

## Current testability limit

Direct host tests for these passive headers are currently blocked because the include chain still reaches Saturn-specific SRL/SGL headers such as `sgl.h`.

Current safe validation path:

- compile-only validation with the real SH2 toolchain
- full project stable-build validation

Hook:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`

Memory-debug presentation boundary inventory:

- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_BOUNDARY_INVENTORY.md`
- `MEMORY_BUDGET_MEMORY_DEBUG_PRESENTATION_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

## Next low-risk steps

1. Isolate a narrower pure-data export layer if host tests become necessary
2. Only then consider a second local observability assembly point in `GameLoopSystem`
3. Keep broader scheduler/reuse aggregation above the now-live track-only reuse
   seam compile-only until the symmetric simulation-side replacement is obvious

## Validation coverage closed in this step

The observability-specific compile-only hook now validates the passive include chain for:

- `src/game_loop_observability_contracts.hpp`
- `src/game_loop_observability_state_assembler.hpp`
- `src/game_loop_observability_packet_assembler.hpp`
- `src/game_loop_memory_presentation_packet_assembler.hpp`
- `src/game_loop_memory_trace_packet_assembler.hpp`
- `src/game_loop_memory_trace_text_contracts.hpp`
- `src/game_loop_memory_trace_text_assembler.hpp`
- `src/game_loop_memory_overlay_text_contracts.hpp`
- `src/game_loop_memory_overlay_text_assembler.hpp`
- `src/game_loop_memory_debug_contracts.hpp`
- `src/game_loop_memory_debug_packet_assembler.hpp`
- `src/game_loop_observability_debug_contracts.hpp`
- `src/game_loop_observability_debug_packet_assembler.hpp`
- `src/game_loop_overlay_debug_text_contracts.hpp`
- `src/game_loop_overlay_debug_text_assembler.hpp`
- `src/game_loop_overlay_debug_contracts.hpp`
- `src/game_loop_overlay_debug_packet_assembler.hpp`
- `src/game_loop_presentation_debug_contracts.hpp`
- `src/game_loop_presentation_debug_assembler.hpp`
- `src/game_loop_render_debug_contracts.hpp`
- `src/game_loop_render_debug_assembler.hpp`
- `src/game_loop_car_visual_debug_contracts.hpp`
- `src/game_loop_car_visual_debug_assembler.hpp`
- `src/game_loop_track_render_debug_contracts.hpp`
- `src/game_loop_track_render_debug_assembler.hpp`
- `src/game_loop_presenter_render_debug_contracts.hpp`
- `src/game_loop_presenter_render_debug_assembler.hpp`
- `src/game_loop_presenter_overlay_debug_contracts.hpp`
- `src/game_loop_presenter_overlay_debug_assembler.hpp`
- `src/game_loop_presenter_input_summary_contracts.hpp`
- `src/game_loop_presenter_input_summary_assembler.hpp`
- `src/game_loop_presenter_facade_contracts.hpp`
- `src/game_loop_presenter_facade_assembler.hpp`
- `src/game_loop_presenter_facade_interface_contracts.hpp`
- `src/game_loop_presenter_facade_interface_assembler.hpp`
- `src/game_loop_presenter_input_contracts.hpp`
- `src/game_loop_presenter_input_assembler.hpp`
- `src/game_loop_track_render_packet.hpp`
- `src/game_loop_track_render_packet_assembler.hpp`
- `src/game_loop_car_visual_packet.hpp`
- `src/game_loop_car_visual_packet_assembler.hpp`

This keeps the next observability cuts outside the critical frame loop while still catching include/regression breaks with the SH2 toolchain.

## Newly consolidated presenter input

The passive presenter-facing side now also exposes one higher-level aggregate:

- `src/game_loop_presenter_input_contracts.hpp`
- `src/game_loop_presenter_input_assembler.hpp`

Current effect:

- one off-path `PresenterInputBundle` can now carry:
  - `PresentationDebugBundle`
  - `RenderFrameDebugBundle`
  - `PresenterRenderDebugPacket`
  - `OverlayDebugBundle`
  - `PresenterOverlayDebugPacket`
  - `ObservabilityDebugBundle`
  - `PresenterInputSummaryPacket`
- one compile-only overload path now also accepts:
  - `OverlayDebugBundle`
  - `ObservabilityDebugBundle`
  - `SchedulerReuseDebugTelemetryPacket`
  and narrows them directly into `PresenterObservabilityInputPacket`
- future presenter/facade extraction can consume one stable aggregate input
  instead of rebuilding cross-domain debug/presentation dependencies at the host
- runtime execution remains untouched

The presenter-facing side now also exposes one reduced render summary:

- `src/game_loop_presenter_render_debug_contracts.hpp`
- `src/game_loop_presenter_render_debug_assembler.hpp`

Current effect:

- one off-path `PresenterRenderDebugPacket` can now carry:
  - `TrackRenderDebugPacket`
  - `CarVisualDebugPacket`
  - top-level `hasTrack` / `hasCar` / `hasRenderableWork`
- the future presenter can consume a narrower render summary without walking the
  full `RenderFrameDebugBundle`
- runtime execution remains untouched

The presenter-facing side now also exposes one reduced overlay/observability summary:

- `src/game_loop_presenter_overlay_debug_contracts.hpp`
- `src/game_loop_presenter_overlay_debug_assembler.hpp`

Current effect:

- one off-path `PresenterOverlayDebugPacket` can now carry:
  - overlay flow presence
  - telemetry flow presence
  - segment/window summary
  - query/wall-query summary
  - frame/memory debug presence flags
- the future presenter can consume a narrower overlay/debug summary without
  walking both `OverlayDebugBundle` and `ObservabilityDebugBundle`
- runtime execution remains untouched

The presenter-facing side now also exposes one top-level input summary:

- `src/game_loop_presenter_input_summary_contracts.hpp`
- `src/game_loop_presenter_input_summary_assembler.hpp`

Current effect:

- one off-path `PresenterInputSummaryPacket` can now carry:
  - presentation/HUD presence
  - render/overlay/observability presence
  - scheduler/reuse debug presence
  - top-level speed/gear/rpm summary
  - top-level face/query counters
- current summary packet shape now includes:
  - `hasSchedulerReuseDebug`
- the future presenter facade can branch on one narrow summary packet before
  touching any deeper passive bundle
- runtime execution remains untouched

The presenter-facing side now also exposes one current top boundary that still
exists in this branch:

- `src/game_loop_presenter_summary_observability_contracts.hpp`
- `src/game_loop_presenter_summary_observability_assembler.hpp`
- `src/game_loop_presenter_presence_decision_contracts.hpp`
- `src/game_loop_presenter_presence_decision_assembler.hpp`
- `src/game_loop_presenter_presence_preview_contracts.hpp`
- `src/game_loop_presenter_presence_preview_assembler.hpp`

Current effect:

- one off-path `PresenterSummaryObservabilityPacket` can now carry:
  - `PresenterInputSummaryPacket`
  - `PresenterObservabilityInputPacket`
- one off-path `PresenterPresenceDecisionPacket` can now narrow that further to:
  - HUD presence decisions
  - render/overlay/observability presence decisions
  - scheduler/reuse debug presence decision
  - memory debug presence decision
- one off-path `PresenterPresencePreviewPacket` now groups:
  - `PresenterSummaryObservabilityPacket`
  - `PresenterPresenceDecisionPacket`
- this is the current highest compile-only presenter packet still backed by
  source files in `src/`
- future presenter work can branch here without reintroducing the removed
  facade/frame-end shim chain

The presenter-facing side now also exposes one observability-side adapter:

- `src/game_loop_presenter_observability_input_contracts.hpp`
- `src/game_loop_presenter_observability_input_assembler.hpp`
- `src/game_loop_presenter_scheduler_reuse_bridge_assembler.hpp`
- `src/game_loop_presenter_scheduler_reuse_preview_contracts.hpp`
- `src/game_loop_presenter_scheduler_reuse_preview_assembler.hpp`

Current effect:

- one off-path `PresenterObservabilityInputPacket` can now carry:
  - `OverlayDebugBundle`
  - `PresenterOverlayDebugPacket`
  - `ObservabilityDebugBundle`
  - `SchedulerReuseDebugTelemetryPacket`
- one compile-only sibling adapter can now derive that scheduler/reuse debug
  input directly from:
  - `SimulationSchedulerTelemetryViewPacket`
  - producer flags
  - `FrameReuseRuntimeOwnerPacket`
  - optional lifecycle aggregate
- future presenter/debug input integration can attach scheduler/reuse debug
  telemetry through one stable adapter instead of widening the broader
  presenter input boundary
- current packet shape stays narrow:
  - `overlayDebug`
  - `hasObservability`
  - `hasSchedulerReuseDebug`
- runtime execution remains untouched

One compile-only preview now also exists directly above that bridge:

- `PresenterInputSummaryPacket`
- `PresenterObservabilityInputPacket`
- `SchedulerReuseDebugTelemetryPacket`

Current effect:

- one off-path `PresenterSchedulerReusePreviewPacket` can validate the local
  attach point where scheduler/reuse debug reaches the presenter-facing summary
  side
- the preview can be built either from:
  - direct `SchedulerReuseDebugTelemetryPacket`
  - scheduler telemetry + producer flags + `frame_reuse` runtime-owner state
  - optional lifecycle aggregate
- runtime execution remains untouched

One compile-only preview now also exists one level above that bridge:

- `src/game_loop_presenter_summary_scheduler_reuse_preview_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_preview_assembler.hpp`

Current effect:

- one off-path `PresenterSummarySchedulerReusePreviewPacket` can validate the
  alignment between:
  - `PresenterSummaryObservabilityPacket`
  - `PresenterSchedulerReusePreviewPacket`
- this keeps the scheduler/reuse attach path visible at the same presenter
  summary level where later presence decisions branch
- runtime execution remains untouched

One compile-only top presenter preview now also exists above both current local
presenter branches:

- `src/game_loop_presenter_boundary_preview_contracts.hpp`
- `src/game_loop_presenter_boundary_preview_assembler.hpp`

Current effect:

- one off-path `PresenterBoundaryPreviewPacket` can validate the alignment
  between:
  - `PresenterPresencePreviewPacket`
  - `PresenterSummarySchedulerReusePreviewPacket`
- this creates one compile-only top presenter boundary that still stays fully
  outside `src/game_loop_system.hpp`
- runtime execution remains untouched

One minimal presenter-side view now also exists above that preview:

- `src/game_loop_presenter_boundary_view_contracts.hpp`
- `src/game_loop_presenter_boundary_view_assembler.hpp`

Current effect:

- one off-path `PresenterBoundaryViewPacket` can narrow the current top preview
  to:
  - direct presence decisions
  - `hasObservability`
  - `hasSchedulerReuseDebug`
  - speed/gear/rpm
  - frame id
- this is the next safe compile-only attach for a future non-critical textual
  presenter helper
- runtime execution remains untouched

One compile-only textual presenter helper now also exists above that view:

- `src/game_loop_presenter_boundary_text_contracts.hpp`
- `src/game_loop_presenter_boundary_text_assembler.hpp`
- `src/game_loop_presenter_boundary_view_presenter_ops.hpp`

Current effect:

- one off-path `PresenterBoundaryTextPacket` can narrow the current top view to:
  - compact status text payload
  - compact decision text payload
- one off-path helper can print:
  - compact speed/gear/rpm + observability/scheduler flags
  - compact presenter decision flags
- this keeps the first future retry localized to a narrow presenter-side view
  consumer instead of the broader preview chain
- runtime execution remains untouched

The presenter-facing side now also exposes one facade-ready packet:

- `src/game_loop_presenter_facade_contracts.hpp`
- `src/game_loop_presenter_facade_assembler.hpp`

Current effect:

- one off-path `PresenterFacadePacket` can now carry:
  - `PresenterInputSummaryPacket`
  - `DrivingHudTextPacket`
  - `PeriodicHudStatsPacket`
  - `PresenterRenderDebugPacket`
  - `PresenterOverlayDebugPacket`
- the future passive presenter facade can receive one directly consumable packet
  instead of branching first on the broader `PresenterInputBundle`
- runtime execution remains untouched

The presenter-facing side now also exposes one explicit facade interface contract:

- `src/game_loop_presenter_facade_interface_contracts.hpp`
- `src/game_loop_presenter_facade_interface_assembler.hpp`
- `GAME_LOOP_PRESENTER_FACADE_PLAN.md`

Current effect:

- one off-path `PresenterFacadeRequestPacket` now describes the non-owning
  handoff into a future facade
- one off-path `PresenterFacadeDecisionPacket` now describes the first safe
  decision surface for runtime-neutral facade integration
- the safe substitution order for a future `GameLoopPresenterFacade` is now
  documented independently from the broader observability plan
- the full passive presenter inventory is now centralized in
  `PRESENTER_PASSIVE_REFACTOR_INVENTORY.md`

The presenter-facing side now also exposes one narrower facade input adapter:

- `src/game_loop_presenter_facade_input_contracts.hpp`
- `src/game_loop_presenter_facade_input_assembler.hpp`

Current effect:

- one off-path `PresenterFacadeInputPacket` can now carry:
  - `PresenterInputSummaryPacket`
  - `DrivingHudTextPacket`
  - `PeriodicHudStatsPacket`
  - `PresenterRenderDebugPacket`
  - `PresenterObservabilityInputPacket`
- future facade assembly can consume one narrower boundary above presenter input
  and below the final facade packet
- runtime execution remains untouched

The render-facing passive side now also exposes one aggregate bundle:

- `src/game_loop_render_debug_contracts.hpp`
- `src/game_loop_render_debug_assembler.hpp`

Current effect:

- one off-path `RenderFrameDebugBundle` can now carry:
  - `TrackRenderFramePacket`
  - `TrackRenderDebugPacket`
  - `CarVisualFramePacket`
  - `CarVisualDebugPacket`
- the future presenter/render-facade path can consume one stable visual/render
  aggregate before any ownership change in the live loop
- runtime execution remains untouched
