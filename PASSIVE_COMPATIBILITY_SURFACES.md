# Passive Compatibility Surfaces

## Objective

Record passive headers that still exist in the tree even after their live
runtime consumers were removed, so future refactors do not break smoke
validation or compile-only boundaries by accident.

## Categories

### Compatibility shim

Definition:

- no live runtime dependency remains
- header stays because passive/observability smoke validation still compiles it
- removal requires updating the validation surface, not only runtime callers

Current entries:

- none currently classified

Current state:

- the former compile-only preview pair was removed entirely after it became
  fully orphaned
- the former presenter facade/decision shim chain was isolated from the generic
  smoke scripts and then removed entirely once it had no runtime, compile-only,
  or remaining smoke-only consumers

Safe removal precondition:

- first update smoke-validation inputs so they no longer require this surface

### Passive compatibility surface

Definition:

- no live runtime consumer remains in the current narrowed path
- header still exists as a passive contract/adapter layer
- removal is safe only after checking compile-only and smoke-only users

Current entries:

- `src/game_loop_reuse_runtime_observability_ops.hpp`
- `src/frame_reuse_runtime_owner_contracts.hpp`
- `src/frame_reuse_runtime_owner_assembler.hpp`
- `src/frame_reuse_runtime_observability_source_assembler.hpp`
- `src/frame_reuse_runtime_observability_owner_assembler.hpp`
- `src/game_loop_reuse_runtime_source_assembler.hpp`
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
- `src/game_loop_reuse_runtime_preview_contracts.hpp`
- `src/game_loop_reuse_runtime_preview_assembler.hpp`
- `src/game_loop_reuse_runtime_owner_assembler.hpp`
- `src/game_loop_reuse_runtime_debug_bridge_assembler.hpp`
- `src/game_loop_scheduler_reuse_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_observability_assembler.hpp`
- `src/game_loop_scheduler_reuse_flow_observability_contracts.hpp`
- `src/game_loop_scheduler_reuse_flow_observability_assembler.hpp`
- `src/game_loop_scheduler_reuse_observability_assembly_ops.hpp`
- `src/game_loop_scheduler_reuse_debug_telemetry_contracts.hpp`
- `src/game_loop_scheduler_reuse_debug_telemetry_assembler.hpp`
- `src/game_loop_scheduler_reuse_preview_contracts.hpp`
- `src/game_loop_scheduler_reuse_preview_assembler.hpp`
- `src/game_loop_track_render_presentation_preview_contracts.hpp`
- `src/game_loop_track_render_presentation_preview_assembler.hpp`
- `src/game_loop_scheduler_track_render_preview_contracts.hpp`
- `src/game_loop_scheduler_track_render_preview_assembler.hpp`
- `src/game_loop_simulation_scheduler_lifecycle_runtime_assembler.hpp`
- `src/game_loop_track_render_sh2_presentation_runtime_assembler.hpp`

Current state:

- use this category when a boundary has no live runtime consumer, but still has
  some compile-only or passive non-smoke include path to audit before removal
- `src/game_loop_reuse_runtime_observability_ops.hpp` currently prepares the
  first host-local passive adapter for scheduler/reuse observability, but
  runtime state is not yet wired through `src/game_loop_system.hpp`
- the current host seam is source-state first, assembly second; this is
  intentional because no live `frame_reuse` state is owned by the runtime path
  yet
- `src/frame_reuse_observability_source_*` now defines the domain-level raw
  snapshot that can become the future real origin before the game-loop owner
  adapter is switched live
- `src/frame_reuse_runtime_owner_*` now defines the passive runtime-side owner
  packet that can later sit next to real producer/history ownership
- `src/frame_reuse_runtime_observability_source_assembler.hpp` now isolates the
  first narrowing from runtime-owner packet into observability source snapshot
- `src/frame_reuse_runtime_observability_owner_assembler.hpp` now isolates the
  narrowing from runtime-owner packet into the frame-reuse observability owner
  packet without reopening the host seam
- `src/game_loop_reuse_runtime_source_assembler.hpp` now exposes the sibling
  compile-only bridge from runtime-owner packet into
  `GameLoopObservabilityDomain::ReuseObservabilitySourcePacket`
- `src/frame_reuse_observability_source_owner_*` now adds the domain-level
  owner wrapper so the future real source can stay entirely outside the
  observability host/adapters
- `src/frame_reuse_observability_capture_ops.hpp` now centralizes the passive
  capture seam that can later sit next to real producer/history ownership
- `src/game_loop_reuse_source_state_*` prepares the future owned source
  boundary outside the critical loop, so the first real runtime source can be
  introduced without broadening `src/game_loop_system.hpp`
- `src/game_loop_reuse_source_owner_*` now adds the compile-only owner layer
  above that source boundary, keeping the first future real owner outside the
  host critical loop
- `src/game_loop_reuse_runtime_observability_*` now splits reuse source-state
  contracts, packet assembly, and debug-bundle assembly into narrower passive
  helpers under the existing host-local seam
- `src/game_loop_reuse_runtime_preview_*` now defines the current compile-only
  preview directly above the `frame_reuse` runtime-owner to reuse-debug bridge
- `src/game_loop_reuse_runtime_owner_assembler.hpp` now adds the compile-only
  owner bridge from `FrameReuseRuntimeOwnerPacket` into the game-loop
  observability owner packet
- `src/game_loop_reuse_runtime_debug_bridge_assembler.hpp` now adds the
  compile-only bridge from `FrameReuseRuntimeOwnerPacket` into
  `ReuseObservabilityAssemblyInputs` and `ReuseObservabilityDebugBundle`
- `src/game_loop_scheduler_reuse_observability_*` now defines the passive
  aggregate above reuse + scheduler telemetry using explicit producer flags
- `src/game_loop_scheduler_reuse_flow_observability_*` now defines the higher
  passive bundle above lifecycle + scheduler/reuse aggregate
- `src/game_loop_scheduler_reuse_observability_assembly_ops.hpp` now exposes
  the compile-only bridge from scheduler telemetry + producer flags +
  `FrameReuseRuntimeOwnerPacket` into the higher scheduler/reuse aggregate
- `src/game_loop_scheduler_reuse_debug_telemetry_*` now exposes the minimal
  valid-only debug/presenter marker above `SchedulerReuseFlowObservabilityPacket`
- `src/game_loop_scheduler_reuse_preview_*` now defines the current
  compile-only preview directly above the scheduler/reuse flow boundary
- `src/game_loop_track_render_presentation_preview_*` now defines the current
  compile-only preview directly above the track-render telemetry/hint/SH2
  presentation seam
- `src/game_loop_scheduler_track_render_preview_*` now defines the current
  compile-only preview directly above the lifecycle + track-render SH2
  presentation bridge
- `src/game_loop_presenter_observability_input_assembler.hpp` now accepts the
  same marker through an overload while preserving the older two-argument
  adapter
- `src/game_loop_presenter_input_assembler.hpp` now exposes the sibling
  compile-only overload that threads the same marker directly into
  `PresenterInputBundle` without reopening the runtime path
- `src/game_loop_presenter_input_summary_assembler.hpp` now mirrors that marker
  at the top-level presenter summary boundary through
  `PresenterInputSummaryPacket.hasSchedulerReuseDebug`
- `src/game_loop_presenter_summary_observability_*` now defines the current
  highest presenter compile-only boundary still backed by live source files in
  this branch
- `src/game_loop_presenter_presence_decision_*` now defines the narrowest
  current presenter presence-decision boundary above
  `PresenterSummaryObservabilityPacket`
- `src/game_loop_presenter_presence_preview_*` now defines the current
  compile-only preview directly above that presenter presence-decision boundary
- `src/game_loop_simulation_scheduler_lifecycle_runtime_assembler.hpp` now
  exposes the sibling compile-only bridge from scheduler runtime packets into
  the lifecycle observability packet
- `src/game_loop_track_render_sh2_presentation_runtime_assembler.hpp` now
  exposes the compile-only bridge from SH2 snapshot/lifecycle inputs into the
  final track-render SH2 presentation packet

Safe removal precondition:

- confirm no smoke header or compile-only chain still includes it

### Compile-only active surface

Definition:

- not part of the live runtime path
- still intentionally used by compile-only preview or passive validation flow
- should not be removed as "dead" without replacing its compile-only role

Current entries:
- none currently classified

Current state:
- there is no currently classified entry in this bucket
- the former compile-only preview pair became completely orphaned after the
  bridge and preview-subpacket cuts, so it was removed instead of being kept as
  passive surface

Safe narrowing direction:

- when a compile-only surface loses all non-self consumers and is not compiled
  by smoke validation, prefer removing it instead of preserving it as shim

## Refactor rule

Before deleting a passive header, classify it first:

1. compatibility shim
2. passive compatibility surface
3. compile-only active surface

Only category 1 is safe to leave orphaned from runtime.
Category 2 needs include-audit first.
Category 3 needs a replacement compile-only path first.
