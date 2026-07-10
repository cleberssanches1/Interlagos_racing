# Frame Reuse Observability Boundary Consolidated

## Objective

Record the current consolidated passive state of the `frame_reuse` observability
boundary.

This document is inventory-only.

It does not authorize runtime ownership changes by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the critical runtime host
- one narrow live track-only producer/history seam is now wired through the host
- host sequencing, audio timing, and render pacing remain unchanged

## Consolidated boundary

The `frame_reuse` observability path is now split into helper
families:

### 1. Runtime-owner staging

Files:

- `src/frame_reuse_runtime_owner_contracts.hpp`
- `src/frame_reuse_runtime_owner_assembler.hpp`

Role:

- define the narrow runtime-side owner packet
- stage:
  - `SimulationFrameHistoryState`
  - `TrackFrameHistoryState`
  - `SimulationReuseDecisionPacket`
  - `TrackReuseDecisionPacket`
  - `FrameReuseTelemetry`

### 2. Runtime-to-observability narrowing

Files:

- `src/frame_reuse_runtime_observability_source_assembler.hpp`
- `src/frame_reuse_runtime_observability_owner_assembler.hpp`
- `src/game_loop_reuse_runtime_source_assembler.hpp`
- `src/game_loop_reuse_runtime_owner_assembler.hpp`

Role:

- narrow `FrameReuseRuntimeOwnerPacket` into:
  - `ReuseObservabilitySourceSnapshot`
  - `FrameReuseDomain::ReuseObservabilitySourceOwnerPacket`
  - `GameLoopObservabilityDomain::ReuseObservabilitySourcePacket`
  - `GameLoopObservabilityDomain::ReuseObservabilitySourceOwnerPacket`

### 3. Source capture facade

Files:

- `src/frame_reuse_observability_source_contracts.hpp`
- `src/frame_reuse_observability_source_assembler.hpp`
- `src/frame_reuse_observability_source_owner_contracts.hpp`
- `src/frame_reuse_observability_source_owner_assembler.hpp`
- `src/frame_reuse_observability_capture_ops.hpp`

Role:

- expose passive capture helpers for:
  - `ReuseObservabilitySourceSnapshot`
  - `FrameReuseDomain::ReuseObservabilitySourceOwnerPacket`
  - `FrameReuseRuntimeOwnerPacket`

### 4. Game-loop observability bridge

Files:

- `src/game_loop_reuse_source_state_contracts.hpp`
- `src/game_loop_reuse_source_state_assembler.hpp`
- `src/game_loop_reuse_source_owner_contracts.hpp`
- `src/game_loop_reuse_source_owner_assembler.hpp`
- `src/game_loop_reuse_runtime_observability_contracts.hpp`
- `src/game_loop_reuse_runtime_packet_assembler.hpp`
- `src/game_loop_reuse_runtime_debug_bundle_assembler.hpp`
- `src/game_loop_reuse_runtime_observability_ops.hpp`
- `src/game_loop_reuse_runtime_debug_bridge_assembler.hpp`

Role:

- expose source-state and assembly-input contracts
- build `ReuseObservabilityPacket`
- build `ReuseObservabilityDebugBundle`
- bridge a future `FrameReuseRuntimeOwnerPacket` directly into the game-loop
  observability input/debug path without rebuilding that chain in the host
- keep host-local capture and assembly seams explicit

### 5. Compile-only runtime-to-presentation preview

Files:

- `src/game_loop_reuse_runtime_preview_contracts.hpp`
- `src/game_loop_reuse_runtime_preview_assembler.hpp`

Role:

- define one preview packet directly above the current runtime-owner to
  debug-bundle bridge
- group:
  - `FrameReuseDomain::FrameReuseRuntimeOwnerPacket`
  - `GameLoopObservabilityDomain::ReuseObservabilityDebugBundle`
- keep one narrow compile-only inspection point that spans raw source ownership
  and presenter-facing reuse debug payload without touching the host runtime

## Low live seam now active

One low live seam now exists at the host boundary:

- `src/game_loop_system.hpp` now owns one narrow
  `GameLoopRuntime::TrackReuseRuntimeState`
- the track flow captures:
  - `requestFrameId`
  - `activeSegmentId`
  - `renderEnabled`
  - `producerJobInFlight`
  - committed `TrackFrameHistoryState`
- `TryBuildReuseObservabilityDebugBundle(...)` now replaces the previous empty
  capture with one real track-only `FrameReuseRuntimeOwnerPacket`
- simulation-side reuse stays neutral in the same seam
- cumulative reuse telemetry stays outside the live runtime path

## What still stays in the host

The following ownership remains local to `src/game_loop_system.hpp`:

- cadence of reuse debug presentation
- local call to `TryBuildReuseObservabilityDebugBundle(...)`
- local call ordering around overlay/debug presentation
- selection of when observability is presented
- the narrow track-only runtime snapshot/state that feeds the accepted seam

## Functional coverage

The consolidated boundary now covers the shape from runtime source to
presenter-ready reuse bundle:

- runtime owner packet
- source snapshot
- frame-reuse-domain owner packet
- game-loop source packet
- source state
- assembly inputs
- reuse observability packet
- reuse observability debug bundle
- runtime-to-presentation preview packet

## Why this boundary is considered consolidated

It now has:

- explicit runtime-owner staging
- explicit sibling runtime narrowing branches
- explicit source capture facade
- explicit game-loop bridge contracts
- explicit packet/debug-bundle assembly helpers
- explicit compile-only preview above the runtime/debug bridge
- host-local ownership preserved
- only one narrow live track-only source activation
- no broad runtime ownership migration

## Recommended next moves

Do next:

1. keep this boundary runtime-stable
2. complete only the missing symmetric simulation-side branch if a clear
   remove-first seam exists
3. use the same pattern on another narrow Scheduler/Reuse boundary
4. avoid reopening `src/game_loop_system.hpp` for a broader aggregate until a
   single-call-site retry is justified

Do not do next:

- wire live `SimulationFrameHistoryState` into the host in one patch
- couple this boundary with audio/render/bootstrap work
- skip directly from runtime-owner staging to a broad scheduler/reuse live retry
