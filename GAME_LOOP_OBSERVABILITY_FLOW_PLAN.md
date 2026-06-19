# Game Loop Observability Flow Plan

## Objective

Prepare a future extraction of observability/debug presentation out of `src/game_loop_system.hpp` without touching the critical runtime yet.

## Current passive building blocks

### Presentation

- `src/game_loop_presentation_ops.hpp`

Provides passive builders for:

- `FramePresentationSnapshot`
- `Sh2SplitTelemetrySnapshot`

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

## Next low-risk steps

1. Add compile-only SH2 validation hooks for passive observability headers
2. Rehearse one textual integration point without changing behavior
3. Isolate a narrower pure-data export layer if host tests become necessary
4. Only then consider a local observability assembly method in `GameLoopSystem`
