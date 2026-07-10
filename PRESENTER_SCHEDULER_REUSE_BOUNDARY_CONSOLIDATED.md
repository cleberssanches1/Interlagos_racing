# Presenter Scheduler/Reuse Boundary Consolidated

## Objective

Consolidate the current compile-only presenter boundary that receives
scheduler/reuse observability, without reopening runtime ownership in
`src/game_loop_system.hpp`.

## Boundary hierarchy

Lowest narrowed attach point:

- `SchedulerReuseDebugTelemetryPacket`

Presenter observability attach:

- `PresenterObservabilityInputPacket`

Presenter summary boundary:

- `PresenterSummaryObservabilityPacket`

Local presenter decision branch:

- `PresenterPresenceDecisionPacket`
- `PresenterPresencePreviewPacket`

Scheduler/reuse summary branch:

- `PresenterSchedulerReusePreviewPacket`
- `PresenterSummarySchedulerReusePreviewPacket`

Current highest compile-only presenter boundary:

- `PresenterBoundaryPreviewPacket`

Minimal presenter-side helper attach above that:

- `PresenterBoundaryViewPacket`
- `PresenterBoundaryTextPacket`
- `PresentPresenterBoundaryViewPacket(...)`
- `PresentPresenterBoundaryDecisionPacket(...)`

## Files

Lower attach / summary:

- `src/game_loop_presenter_observability_input_contracts.hpp`
- `src/game_loop_presenter_observability_input_assembler.hpp`
- `src/game_loop_presenter_scheduler_reuse_bridge_assembler.hpp`
- `src/game_loop_presenter_scheduler_reuse_preview_contracts.hpp`
- `src/game_loop_presenter_scheduler_reuse_preview_assembler.hpp`
- `src/game_loop_presenter_summary_observability_contracts.hpp`
- `src/game_loop_presenter_summary_observability_assembler.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_preview_contracts.hpp`
- `src/game_loop_presenter_summary_scheduler_reuse_preview_assembler.hpp`

Local decision / top preview:

- `src/game_loop_presenter_presence_decision_contracts.hpp`
- `src/game_loop_presenter_presence_decision_assembler.hpp`
- `src/game_loop_presenter_presence_preview_contracts.hpp`
- `src/game_loop_presenter_presence_preview_assembler.hpp`
- `src/game_loop_presenter_boundary_preview_contracts.hpp`
- `src/game_loop_presenter_boundary_preview_assembler.hpp`
- `src/game_loop_presenter_boundary_view_contracts.hpp`
- `src/game_loop_presenter_boundary_view_assembler.hpp`
- `src/game_loop_presenter_boundary_text_contracts.hpp`
- `src/game_loop_presenter_boundary_text_assembler.hpp`
- `src/game_loop_presenter_boundary_view_presenter_ops.hpp`

## What this boundary proves

- presenter summary and presence decisions can stay compile-only
- scheduler/reuse debug can reach the same presenter summary level safely
- the top presenter branch can now be validated without rebuilding lower
  observability joins in multiple places
- one future non-critical presenter helper can consume a narrow textual view
  packet instead of the broader preview hierarchy
- one compile-only text packet and textual consumer now exist for that narrow
  view
- runtime ownership remains unchanged

## Runtime status

- compile-only only
- no new runtime members
- no new ownership changes
- no live substitution in `src/game_loop_system.hpp`

## Safe next step

If this boundary stays stable, the next safe move is not another runtime retry.
The next safe move is either:

- fold this hierarchy into a single presenter-boundary inventory section, or
- prepare one compile-only presenter-side attach for a future non-critical
  facade/presenter debug helper

The minimal future runtime reopening for this exact path is now documented in:

- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

The newer simulation-side presenter/resumo compile-only branch above
scheduler/reuse is now documented separately in:

- `PRESENTER_SUMMARY_SCHEDULER_REUSE_SIMULATION_BOUNDARY_CONSOLIDATED.md`
- `PRESENTER_SUMMARY_SCHEDULER_REUSE_SIMULATION_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

## Guard rails

- keep the ISO at `4134912`
- do not reintroduce removed presenter facade/frame-end runtime shims
- do not widen runtime state in `src/game_loop_system.hpp`
- prefer compile-only validation first
