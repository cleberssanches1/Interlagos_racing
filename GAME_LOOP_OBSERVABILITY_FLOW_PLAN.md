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

## Runtime guard rails for the next patch

When the first runtime integration happens, keep all of the following true:

- all new observability packets stay stack-local;
- `GameLoopSystem` gains no new long-lived observability state;
- no new arrays, vectors or heap allocations are introduced;
- no new cross-frame caching is added;
- no Master/Slave scheduling behavior changes in the same patch;
- no audio, HUD or drivetrain behavior changes in the same patch;
- validate clean build and stable ISO immediately after the change.

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
- the future presenter facade can branch on one narrow summary packet before
  touching any deeper passive bundle
- runtime execution remains untouched

The presenter-facing side now also exposes one observability-side adapter:

- `src/game_loop_presenter_observability_input_contracts.hpp`
- `src/game_loop_presenter_observability_input_assembler.hpp`

Current effect:

- one off-path `PresenterObservabilityInputPacket` can now carry:
  - `OverlayDebugBundle`
  - `PresenterOverlayDebugPacket`
  - `ObservabilityDebugBundle`
  - `SchedulerReuseDebugTelemetryPacket`
- future presenter/debug input integration can attach scheduler/reuse debug
  telemetry through one stable adapter instead of widening the broader
  presenter input boundary
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
