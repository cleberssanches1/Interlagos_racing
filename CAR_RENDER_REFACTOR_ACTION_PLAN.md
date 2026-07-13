# Car Render Refactor Action Plan

## Objective

Reopen `car render` with a strictly substitutional plan that improves
separation of responsibilities without breaking these runtime invariants:

- ISO remains exactly `4134912`
- emulator boot remains stable
- `src/game_loop_system.hpp` keeps `RenderCar(...)` ownership
- final render submission stays on Master SH2
- shadow draw ordering and draw entrypoints stay unchanged in the first cuts

This plan is intentionally narrower than a full `CarVisualFramePacket`
integration because that wider retry already exceeded the fixed ISO envelope.

## Current accepted baseline

Live runtime seam already in place:

- `GameLoopRuntime::BuildCarRenderRuntimePacket(...)`
- `GameLoopRuntime::BuildCarShadowRuntimeDecision(...)`
- `GameLoopRuntime::BuildCarShadowPrepPacket(...)`
- `GameLoopRuntime::ApplyCarRenderRuntimeSync(...)`
- `GameLoopRuntime::SubmitCarRenderRuntime(...)`

Primary live owner:

- `src/game_loop_system.hpp`

Primary passive graph already available:

- `src/car_render_system.hpp`
- `src/game_loop_car_visual_packet.hpp`
- `src/game_loop_car_visual_packet_assembler.hpp`
- `src/game_loop_car_visual_debug_assembler.hpp`

## Progress snapshot

Completed from this plan:

- Phase 2 first cut
  - shadow debug capture is now explicit in the draw path
  - shadow yaw-to-angle conversion is now pure
  - draw ownership and render behavior remain unchanged
- Phase 1 first cut
  - `RenderCar(...)` now resolves runtime inputs through one narrow helper
  - render-position, visual yaw offset, runtime debug, gameplay yaw and
    depth-bias choice are no longer spread inline in the host call site
  - `lastValidCarRenderPos_` ownership remains in `src/game_loop_system.hpp`
  - `RenderPacket` construction still goes through the current runtime helper path
- Phase 3 first cut
  - shadow prep now has one explicit `TryBuild...` seam between decision and draw
  - `RenderCarShadowIfEnabled(...)` no longer inlines `enabled + build + draw`
    glue at both blob/model branches
  - draw entrypoints remain unchanged
  - shadow packet assembly still stays on the current runtime path
- Phase 4 first cut
  - rendered face-count capture is now explicit apart from submit execution
  - telemetry shaping is now explicit apart from face-count capture
  - Master-side submit ownership remains unchanged
  - the host call site remains unchanged
- Phase 5 resolved as formal freeze
  - `CarRenderPrepareTask` currently normalizes yaw only
  - `CarPrepareRuntimeState.outputYaw` has no live consumer in `src/`
  - the feature flag is already disabled by default in `src/main.cxx`
  - removing the path from the critical host now would create more runtime risk
    than value
  - the accepted action is to freeze this SH2 branch as non-expanding until a
    real pre-render payload exists

Still open:

- no mandatory phase remains on the current `car render` plan

## Runtime problems that still justify refactoring

### 1. `RenderCar(...)` is still a mixed orchestration point

`src/game_loop_system.hpp` still mixes:

- runtime state lookup
- render policy constants
- packet assembly calls
- shadow dispatch
- sync handoff
- submit/telemetry handoff

This is acceptable for the current narrow seam, but it is still too much
responsibility in one host function.

### 2. Shadow draw preparation still mixes rendering and debug mutation

`BuildShadowDrawYaw(...)` currently:

- converts shadow yaw to render angle
- mutates debug state through `StoreShadowDebugState(...)`

That makes the draw path less explicit and harder to isolate cleanly.

### 3. Shadow packet construction is still coupled to dispatch branching

`RenderCarShadowIfEnabled(...)` still:

- owns the blob/model decision
- builds the shadow packet at dispatch time
- calls the draw entrypoints directly

This is narrow enough to be safe today, but still couples decision, assembly
and dispatch in one place.

### 4. Submit runtime helper still combines driver ownership and telemetry packaging

`SubmitCarRenderRuntime(...)` correctly stays Master-side, but it still merges:

- pipeline reset/flush
- mesh submission
- rendered-face capture
- telemetry construction

That boundary should stay live, but its internal split can become cleaner.

### 5. The current SH2 prepare path has low value

`CarRenderPrepareTask` currently normalizes only yaw. That is too small to
justify broader architectural decisions around SH2 load split on this axis.

## Refactor strategy

The correct strategy is:

1. keep the current live seam stable
2. keep new work substitutional, not additive
3. flatten host responsibilities before widening packet usage
4. only reopen broader packet handoff if equivalent host logic is removed in
   the same patch

## Execution phases

### Phase 1 - Consolidate host-side responsibilities without changing behavior

Goal:

- make `RenderCar(...)` read like orchestration only

Do:

1. extract a narrow host helper for render input resolution:
   - resolved render position
   - gameplay yaw used by render
   - visual yaw offset
   - runtime debug snapshot
   - depth-bias choice
2. keep `lastValidCarRenderPos_` ownership in `src/game_loop_system.hpp`
3. keep actual `RenderPacket` construction in the current runtime helper path

Do not:

- introduce `CarVisualFramePacket` into live runtime
- move any renderer call
- add persistent state

Expected benefit:

- less policy and state gathering inside `RenderCar(...)`
- cleaner seam for later packet reuse

### Phase 2 - Separate shadow debug capture from draw-angle conversion

Goal:

- remove side effects from the angle conversion path

Do:

1. make one explicit shadow-debug capture step
2. make yaw-to-angle conversion pure
3. keep `DrawCarShadowBlob(...)` and `DrawCarShadowModel(...)` unchanged as draw owners

Do not:

- change projection math
- change Scene2D effects
- change SBA shadow render calls

Expected benefit:

- cleaner shadow flow
- smaller chance of hidden regressions when widening shadow-prep later

### Phase 3 - Flatten shadow dispatch around one prepared pose path

Goal:

- isolate decision, packet assembly and dispatch more clearly

Do:

1. keep `BuildCarShadowRuntimeDecision(...)`
2. assemble prepared shadow data through one narrow local path
3. dispatch blob/model draws from that prepared result
4. avoid duplicated packet-build or mode-branch glue in the host

Do not:

- packetize blob polygon generation
- move draw entrypoints out of `src/game_loop_system.hpp`

Expected benefit:

- shadow-prep becomes easier to widen later
- branch-local host code shrinks without changing ownership

### Phase 4 - Split submit ownership from submit telemetry formatting

Goal:

- keep Master-side submit live, but reduce mixed concerns inside the helper

Do:

1. keep reset / submit / flush in the live Master-side helper
2. separate rendered-face capture from telemetry shaping
3. let telemetry assembly remain passive where possible

Do not:

- move submit ownership off Master
- widen mesh renderer ownership

Expected benefit:

- clearer boundary between driver-facing work and observability work

### Phase 5 - Re-evaluate the value of Slave-side car prepare

Goal:

- stop carrying structural complexity for negligible work

Do:

1. measure whether `CarRenderPrepareTask` remains worth keeping
2. if it still only normalizes yaw, either:
   - freeze it as intentionally trivial, or
   - fold it back if another subsystem needs the budget more
3. only expand it if real pre-render prep becomes externalized

Expected benefit:

- more honest SH2 split
- less fake parallelism in the architecture

## Recommended patch order

Apply in this order:

1. Phase 2
2. Phase 1
3. Phase 3
4. Phase 4
5. Phase 5

Reason:

- Phase 2 is the safest remove-first cleanup
- Phase 1 reduces `RenderCar(...)` surface without widening runtime packets
- Phase 3 reuses the cleaned shadow path
- Phase 4 touches a smaller live seam after render/shadow flow is clearer
- Phase 5 is architectural cleanup, not immediate runtime value

## Validation gate for every runtime patch

For each live change on this subsystem:

1. `powershell -ExecutionPolicy Bypass -File tools\validate_saturn_stable_build.ps1`
2. recreate:
   - `BuildDrop\passive_header_validation`
   - `BuildDrop\observability_header_validation`
3. `powershell -ExecutionPolicy Bypass -File tools\validate_game_loop_passive_headers.ps1`
4. `powershell -ExecutionPolicy Bypass -File tools\validate_game_loop_observability_headers.ps1`
5. remove transient objects if they reappear:
   - `src\car_wheel_rig.o`
   - `src\track_pipeline_stages.o`

## Success criteria

The refactor is successful only if:

- `RenderCar(...)` becomes a thinner orchestrator
- shadow debug capture no longer hides inside angle conversion
- shadow dispatch is flatter and easier to reason about
- Master-side submit ownership remains stable
- ISO stays `4134912`
- emulator boot remains stable

## Explicit non-goals for this plan

This plan does not include:

- full live `CarVisualFramePacket` integration
- `MeshRenderer` ownership migration
- render/audio combined changes
- scheduler/reuse combined changes
- N-1 visual reuse
