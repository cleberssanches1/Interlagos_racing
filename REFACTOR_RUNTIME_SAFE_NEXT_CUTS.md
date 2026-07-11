# Refactor Runtime Safe Next Cuts

## Objective

Provide one operational index of the next runtime-safe cuts already prepared in
the branch, ordered by risk and execution discipline.

This document is intentionally narrower than the general refactor inventory.

It exists to answer one question only:

- what is the next safe runtime cut, and under what guard rails?

## Global guard rails

Every candidate below must preserve:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- remove-first substitution in the same patch
- boundary-local change only

Required validation after any runtime attempt:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Priority order

From safest practical runtime work to most constrained:

1. Memory Budget additional bridge/category consumers
2. Car Render only after an explicit new narrow remove-first slice is prepared
3. CD/bootstrap broader follow-up beyond current accepted narrow cuts
4. Scheduler/Reuse seam join only after size-reduction pass
5. Broad presenter/scheduler/reuse aggregates
6. Broad car visual runtime handoff

## Subsystem decisions

### Presenter

**Current live cut already present**

- one local HUD status-line path already uses
  `PresenterBoundaryStatusTextPacket`

**Exact live boundary**

- `DrivingHudTextPacket`
- `PresenterBoundaryStatusTextPacket`
- `PresentPresenterBoundaryHudStatusTextPacket(...)`

**Why it is acceptable**

- localized to one textual presenter/debug helper
- does not require ownership extraction
- does not require reviving removed facade/frame-end chains

**Current next step**

- test this live cut on emulator
- then either accept it as final or freeze broader presenter runtime work

**Do not mix with**

- facade resurrection
- frame-end chain resurrection
- scheduler/reuse policy changes
- overlay ownership changes

**Status**

- already live in the HUD path
- no broader presenter runtime retry is recommended before test/closure decision

**Primary docs**

- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PRESENTER_BOUNDARY_TEXT_FIRST_STATUS_RETRY_PATCH_PLAN.md`
- `PRESENTER_BOUNDARY_DECISION_RETRY_BLOCKER.md`

### Track Render

**Current live cut already present**

- one local SH2/HUD presentation path already uses
  `TrackRenderSh2PresentationPacket`

**Exact live boundary**

- `TrackRenderTelemetryViewPacket`
- `TrackRenderSh2PresentationPacket`
- `PresentTrackRenderSh2PresentationPacket(...)`

**Why it is acceptable**

- remains in the already stabilized presentation path
- does not move producer ownership
- builds on a boundary with accepted live narrow cuts

**Current next step**

- test the current live SH2 presentation path on emulator
- then either accept it as final or freeze broader Track Render runtime work

**Do not mix with**

- producer/sort scheduling changes
- fallback behavior changes
- broad `TrackRenderPresentationObservabilityPacket` activation first

**Status**

- already live in the HUD/debug path
- no broader Track Render runtime retry is recommended before test/closure decision

**Primary docs**

- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `TRACK_RENDER_PASSIVE_FLOW_PLAN.md`
- `TRACK_REUSE_OBSERVABILITY_FIRST_LIVE_RETRY_PATCH_PLAN.md`

### Scheduler / Reuse

**Current live cuts already present**

- simulation-history commit points are now live
- track-only reuse seam remains live

**Exact blocked boundary**

- combined `simulation + track` owner-packet seam join in
  `TryBuildReuseObservabilityDebugBundle(...)`

**Why it is currently blocked**

- the remaining owner-packet seam join still grows the stable ISO by `2048`
  bytes
- the previously broader retry already showed emulator/runtime integrity risk
- repeating the same join without recovering code budget would reopen the same
  failure class

**Current next step**

- freeze the seam join for now
- only reopen it after a dedicated size-reduction pass or an explicit envelope
  decision

**Do not mix with**

- `SchedulerReuseObservabilityPacket` live consumption first
- `SchedulerReuseFlowObservabilityPacket` live consumption first
- dispatch/drain policy changes
- producer scheduling changes

**Status**

- partial live progress accepted
- owner-packet join remains blocked by stable ISO budget

**Primary docs**

- `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `SIMULATION_REUSE_SEAM_COMPLETION_PLAN.md`
- `SIMULATION_REUSE_LIVE_RETRY_BLOCKER.md`

### Car Render

**Current live cut already present**

- one local shadow-prep handoff already uses `BuildCarShadowPrepPacket(...)`
  while preserving host-owned draw entrypoints

**Exact boundary**

- prepared shadow position/yaw
- existing draw entrypoints remain:
  - `DrawCarShadowBlob(...)`
  - `DrawCarShadowModel(...)`

**Why it is safe**

- removes duplicated setup logic first
- does not move render ownership
- does not widen submit ownership

**Preconditions**

- remove duplicated local shadow-prep reads from both shadow draw paths
- keep `SubmitCarRender(...)` unchanged
- keep final draw dispatch unchanged

**Do not mix with**

- full `CarVisualFramePacket` runtime handoff
- render submission changes
- scheduler/reuse/audio work

**Status**

- narrow shadow-prep live cut already accepted
- no broader `Car Render` runtime retry is recommended before a fresh
  remove-first preparation step

**Primary docs**

- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`
- `CAR_RENDER_PASSIVE_FLOW_PLAN.md`

### Memory Budget

**Current safe next cut**

- only additional bridge/category consumer substitutions, one boundary at a
  time

**Exact boundary**

- `QueryCategoryPolicy(...)`
- `PreferredPoolForCategory(...)`
- `ShouldAvoidOptionalAllocationsForCategory(...)`

**Why it is safe**

- uses already accepted narrow bridge surfaces
- avoids broad allocator packet ownership in critical hosts

**Preconditions**

- one local gate or pool-choice path must be removed in the same patch
- allocator timing must remain unchanged

**Do not mix with**

- `MemoryBudgetFramePacket` live integration
- allocator ownership moves
- `RunWorkRamMaintenance` migration

**Status**

- bridge-level live cuts already accepted
- next work should remain category-local, not frame-packet based

**Primary docs**

- `MEMORY_BUDGET_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
- `MEMORY_BUDGET_RUNTIME_BRIDGE_BOUNDARY_CONSOLIDATED.md`

### CD / Bootstrap

**Current safe next cut**

- no broader bootstrap cut is recommended before reconfirming the current
  stable narrow SBA/anchor replacements

**Exact boundary**

- current accepted narrow packets:
  - `CdAssetSbaBootstrapDecisionPacket`
  - `CdAssetAnchorBootstrapDecisionPacket`

**Why it is constrained**

- `src/main.cxx` is extremely sensitive
- current safe work here is mainly preserve-and-confirm, not widen

**Preconditions**

- any new retry must stay on one bootstrap boundary only
- one exact branch must be removed in the same patch

**Do not mix with**

- broader `CdAssetBootstrapDecisionPacket` live activation
- broader request/read/parse packet live activation
- unrelated bootstrap sequencing changes

**Status**

- current narrow cuts are already applied and accepted
- not the best next runtime target

**Primary docs**

- `CD_ASSET_MINIMAL_BOOTSTRAP_SUBSTITUTION_PLAN.md`
- `CD_ASSET_PASSIVE_FLOW_PLAN.md`
- `CD_BOOTSTRAP_CHAIN_FLOW_PLAN.md`

### AutoLap

**Current safe next cut**

- none recommended for live runtime now

**Why**

- the domain is structurally packetized, but the best posture remains passive
  until higher-value runtime targets above are exhausted

**Status**

- keep passive / compile-only

**Primary docs**

- `AUTO_LAP_ROUTE_PASSIVE_FLOW_PLAN.md`
- `AUTO_LAP_ROUTE_REINTRODUCTION_STRATEGY.md`

## Recommended execution order

If runtime work resumes, the safest order is:

1. Memory Budget category-local bridge consumer

Only after those:

2. Scheduler/Reuse seam join after size-reduction pass
3. any broader presenter text/decision helper retry
4. any broader scheduler/reuse aggregate retry
5. any broader car visual packet handoff

## Not recommended now

Do not prioritize these next:

- broad scheduler/reuse aggregate live consumption
- broad presenter aggregate live consumption
- full car visual runtime handoff
- broad memory frame-packet live consumption
- broad CD/bootstrap packet live consumption

## Resume rule

When resuming runtime refactor work:

1. start from this document
2. open the subsystem-specific minimal live plan
3. identify the exact equivalent local read/branch to remove first
4. touch only one boundary in one patch
5. validate immediately

## Related documents

- `REFACTOR_CONSOLIDATED_FINAL_INDEX.md`
- `REFACTOR_SUBSYSTEM_RUNTIME_MATRIX.md`
- `REFACTOR_CLOSURE_EXECUTION_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `MEMORY_BUDGET_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
- `CD_ASSET_MINIMAL_BOOTSTRAP_SUBSTITUTION_PLAN.md`
- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`
