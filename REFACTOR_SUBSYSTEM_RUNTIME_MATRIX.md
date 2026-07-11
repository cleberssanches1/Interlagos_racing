# Refactor Subsystem Runtime Matrix

## Objective

Provide one subsystem matrix that answers, for each refactor area:

- what is already live
- what is the next safe runtime cut
- what is the current blocker
- what is the primary host file

This document is operational and resume-oriented.

It does not authorize broader runtime substitutions by itself.

## Global invariants

Every runtime move listed here must preserve:

- ISO exactly `4134912`
- stable emulator startup
- no invalid opcode
- no silent close
- one boundary only per patch
- remove-first substitution in the same patch

Validation after any runtime attempt:

- `tools/validate_game_loop_passive_headers.ps1`
- `tools/validate_game_loop_observability_headers.ps1`
- `tools/validate_saturn_stable_build.ps1 -SkipHostTests`

## Matrix

### Presenter

- **Primary Host File** `src/game_loop_system.hpp`
- **Current Live Cuts** HUD status-line path already uses `PresenterBoundaryStatusTextPacket` through `PrintDrivingHud()`
- **Next Safe Cut** no new presenter runtime cut is recommended before testing and either accepting or freezing the current status-line cut
- **Current Blocker** broader presenter retries previously regressed emulator stability; decision-line retry still lacks a clear remove-first host equivalent
- **Primary Runtime Doc** `PRESENTER_BOUNDARY_TEXT_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

### Track Render

- **Primary Host File** `src/game_loop_system.hpp`
- **Current Live Cuts** `TrackRenderTelemetryViewPacket` sharing, `TrackRenderProducerHintPacket` in-flight hint path, `TrackRenderSh2PresentationPacket` local HUD/debug presentation path
- **Next Safe Cut** no broader Track Render runtime cut is recommended before testing and either accepting or freezing the current local SH2 presentation path
- **Current Blocker** must not widen into producer scheduling, fallback logic, or broader presentation aggregate activation
- **Primary Runtime Doc** `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

### Scheduler / Reuse

- **Primary Host File** `src/game_loop_system.hpp`
- **Current Live Cuts** local `SimulationSchedulerTelemetryViewPacket` assembly, local `TrackRenderProducerStatePacket` observability consumption, track-only reuse seam via `TrackReuseRuntimeState` and on-demand `FrameReuseRuntimeOwnerPacket`, authoritative simulation-history commit points
- **Next Safe Cut** no further live seam join is recommended before a dedicated code-size reduction pass
- **Current Blocker** the narrowed owner-packet seam join still increases the stable ISO by `2048` bytes even after the simulation-history commit was split out and stabilized
- **Primary Runtime Doc** `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

### Memory Budget

- **Primary Host Files** `src/game_loop_system.hpp`, `src/main.cxx`, `src/car_audio_system.hpp`
- **Current Live Cuts** narrow bridge/category accessors, frame-end `MemoryDebugPresentationBundle` local consumer, low-work overlay `MemoryDebugPresentationBundle` local consumer
- **Next Safe Cut** one additional category-local bridge consumer substitution only
- **Current Blocker** must not drift allocator timing or widen into `MemoryBudgetFramePacket` live ownership
- **Primary Runtime Doc** `MEMORY_BUDGET_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

### CD / Bootstrap

- **Primary Host File** `src/main.cxx`
- **Current Live Cuts** narrow SBA load gate and narrow anchor fallback gate through accepted bootstrap bridge replacements
- **Next Safe Cut** no broader cut recommended now; preserve current accepted narrow cuts unless one exact bootstrap branch can be replaced in isolation
- **Current Blocker** bootstrap sequencing in `src/main.cxx` is highly sensitive; broad packet activation is not justified yet
- **Primary Runtime Doc** `CD_ASSET_MINIMAL_BOOTSTRAP_SUBSTITUTION_PLAN.md`

### Car Render

- **Primary Host File** `src/game_loop_system.hpp`
- **Current Live Cuts** local shadow-prep handoff via `BuildCarShadowPrepPacket(...)` feeding `DrawCarShadowBlob(...)` and `DrawCarShadowModel(...)` while keeping draw entrypoints and submit ownership on the host
- **Next Safe Cut** no broader `Car Render` runtime cut is recommended before freezing or explicitly reopening the larger `CarVisualFramePacket` handoff
- **Current Blocker** broad `CarVisualFramePacket` handoff remains constrained by binary budget and submit-path sensitivity even after the accepted shadow-prep cut
- **Primary Runtime Doc** `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`

### AutoLap

- **Primary Host File** `src/game_loop_system.hpp`
- **Current Live Cuts** none significant beyond existing host ownership; domain remains largely passive
- **Next Safe Cut** none recommended now
- **Current Blocker** better runtime value exists in presenter/track/scheduler/car boundaries first
- **Primary Runtime Doc** `AUTO_LAP_ROUTE_REINTRODUCTION_STRATEGY.md`

## Recommended resume order

If runtime work resumes from this matrix, the safest order is:

1. Memory Budget
2. Car Render only if a new narrow remove-first slice is explicitly prepared

Keep these later:

3. Scheduler / Reuse seam join after size-reduction pass
4. CD / Bootstrap widening
5. AutoLap live moves

## Quick interpretation rule

Use this matrix as follows:

1. pick one subsystem
2. open its primary runtime doc
3. locate the exact host read/branch to remove
4. patch only that boundary
5. validate immediately

## Related documents

- `REFACTOR_CONSOLIDATED_FINAL_INDEX.md`
- `REFACTOR_RUNTIME_SAFE_NEXT_CUTS.md`
- `REFACTOR_CLOSURE_EXECUTION_PLAN.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `MEMORY_BUDGET_LIVE_INTEGRATION_INVENTORY.md`
