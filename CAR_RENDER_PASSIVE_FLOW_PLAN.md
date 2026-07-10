# Car Render Passive Flow Plan

## Objective

Prepare the extraction of car visual preparation out of `src/game_loop_system.hpp` without changing current rendering behavior.

## Current passive building blocks

- `src/car_render_contracts.hpp`
- `src/car_render_state_assembler.hpp`
- `src/car_shadow_assembler.hpp`
- `src/car_render_submitter.hpp`
- `src/car_render_transition_ops.hpp`
- `src/car_render_system.hpp`
- `src/game_loop_car_shadow_prep_preview_contracts.hpp`
- `src/game_loop_car_shadow_prep_preview_assembler.hpp`
- `src/game_loop_car_visual_packet.hpp`
- `src/game_loop_car_visual_packet_assembler.hpp`

These files already describe a passive visual path for:

- frame context assembly
- render packet preparation
- shadow packet preparation
- submit packet preparation
- render telemetry packaging
- frame-local visual aggregation

## Binary-budget constraint

The first runtime experiments already proved a hard constraint:

- even a minimal `CarVisualFramePacket` integration inside `RenderCar(...)`
  pushed the ISO from `4134912` to `4136960`
- therefore the next `CarRender` runtime step cannot be additive
- the next runtime step must be substitutional:
  - remove equivalent ad hoc logic first
  - only then consume the passive packet/assembler in the freed budget

This means the current passive work remains valid, but the replacement order
must be sector-neutral.

## Current runtime call sites

Current car render path in `src/game_loop_system.hpp`:

1. `ResolveCarRenderPosition(...)`
2. `BuildCarRenderFrameState(...)`
3. `RenderCarShadowIfEnabled(...)`
4. `car->SyncRenderState(...)`
5. `SubmitCarRender(...)`

Current shadow-specific path:

1. `DrawCarShadowBlob(...)`
2. `DrawCarShadowModel(...)`
3. `StoreShadowDebugState(...)`

## Runtime-to-passive substitution map

### Position and render state

Current runtime responsibility:

1. `ResolveCarRenderPosition(...)`
2. `ApplyCarCameraDepthBias(...)`
3. `ApplyCarVisualLift(...)`
4. `BuildCarRenderFrameState(...)`

Passive replacement coverage already available:

1. `Game::CarRenderSystem::BuildFrameContext(...)`
2. `CarRenderDomain::SeedRenderPacket(...)`
3. `CarRenderDomain::ApplyDepthBias(...)`
4. `CarRenderDomain::ApplyVisualLift(...)`

1:1 data mapping:

- `context_.carWorldPosition` -> `FrameContext.worldPosition`
- `camera.location` -> `FrameContext.cameraLocation`
- `camera.lookTarget` -> `FrameContext.cameraLookTarget`
- `carYawDeg_` / visual yaw -> `FrameContext.gameplayYawDeg` + `visualYawOffsetDeg`
- `car->RuntimeDebug()` -> `FrameContext.runtimeDebug`
- `state.renderPosition` -> `RenderPacket.renderPosition`
- `state.renderYawDeg` -> `RenderPacket.renderYawDeg`
- `state.runtimeDebug` -> `RenderPacket.runtimeDebug`

### Shadow preparation

Current runtime responsibility:

1. `RenderCarShadowIfEnabled(...)`
2. `DrawCarShadowBlob(...)`
3. `DrawCarShadowModel(...)`

Passive replacement coverage already available:

1. `Game::CarRenderSystem::BuildShadowPacket(...)`
2. `CarRenderDomain::SeedShadowPacket(...)`
3. `CarRenderDomain::AnchorShadowToGround(...)`
4. `CarRenderDomain::ConfigureShadowModes(...)`

Current compile-only preview above this slice:

1. `src/game_loop_car_shadow_prep_preview_contracts.hpp`
2. `src/game_loop_car_shadow_prep_preview_assembler.hpp`

Current preview payload:

- `RenderPacket`
- `ShadowPacket`

Important constraint:

- shadow drawing entrypoints should remain in `GameLoopSystem` during the first
  runtime substitution
- only the data assembly should move first

1:1 data mapping for this slice:

- `carFrame.renderPosition` -> `ShadowPacket.shadowPosition`
- `carFrame.renderYawDeg` -> `ShadowPacket.shadowYawDeg`
- `carFrame.runtimeDebug.groundMask/groundTargetY` -> grounded Y anchor logic
- `kUseBlobShadow` -> `ShadowPacket.drawBlob`
- `context_.RenderCarShadowModel()` + renderer presence -> `ShadowPacket.drawModel`

Current duplicated runtime logic inside the shadow slice:

1. anchor-to-ground setup exists in both:
   - `DrawCarShadowBlob(...)`
   - `DrawCarShadowModel(...)`
2. shadow yaw/debug-state setup exists in both:
   - `StoreShadowDebugState(...)`
   - local `shadowYawDeg` extraction

What is still runtime-specific and should stay for the first cut:

- blob polygon footprint generation
- 3D-to-2D projection
- Scene2D draw call ordering/effects
- SBA shadow mesh render call

Measured implication for the next cut:

- this slice is a better candidate than submit/telemetry because it contains
  duplicated setup that can pay for a passive handoff
- however the first safe change must remove duplicated anchor/yaw preparation in
  the same patch that introduces passive shadow assembly

### Submission and telemetry

Current runtime responsibility:

1. `SubmitCarRender(...)`
2. `lastRenderedCarFacesThisFrame_` update

Passive replacement coverage already available:

1. `Game::CarRenderSystem::BuildSubmitPacket(...)`
2. `Game::CarRenderSystem::BuildTelemetry(...)`
3. `CarRenderDomain::SeedSubmitPacket(...)`
4. `CarRenderDomain::MarkSubmitIssued(...)`
5. `CarRenderDomain::RecordRenderedFaceCount(...)`

Important constraint:

- `renderPipeline->Reset/Flush` and `car.SubmitRender(...)` stay on the Master
- telemetry packaging can become passive before submission ownership changes

Measured reality of this slice:

- the current runtime submission path is already very small
- the face-count telemetry is captured from a single call site only
- there is no meaningful duplicated ad hoc logic here comparable to the
  position/bias/lift slice

Implication:

- `SubmitCarRender(...)` is not a good standalone runtime cut right now
- introducing packet/telemetry assembly here by itself is more likely to add
  code than to remove it
- this slice should only move together with a larger equivalent removal:
  - shadow-prep data handoff, or
  - explicit submit ownership reshaping

## Passive packet model

`src/game_loop_car_visual_packet.hpp` now aggregates:

- `FrameContext`
- `RenderPacket`
- `ShadowPacket`
- `SubmitPacket`
- `Telemetry`
- `syncYawDeg`
- `car`

This keeps the future extraction frame-local and non-owning.

## Safe future integration order

### Step 1 - local packet assembly only

Inside `src/game_loop_system.hpp`, assemble:

1. `FrameContext`
2. `RenderPacket`
3. `ShadowPacket`
4. `SubmitPacket`
5. `Telemetry`
6. `CarVisualFramePacket`

Consume immediately in the same function.

No new persistent members.
No async behavior changes.
No ownership transfer.

Current status:

- functionally valid as a concept
- not yet binary-budget valid inside `RenderCar(...)`
- blocked until equivalent code removal pays for the added packet assembly

### Step 2 - keep direct render calls

The first runtime integration must preserve:

- current shadow blob/model calls
- current `SyncRenderState(...)`
- current `SubmitCarRender(...)`

The passive packet should only become the data carrier.

Practical interpretation:

- keep `DrawCarShadowBlob(...)`
- keep `DrawCarShadowModel(...)`
- keep `car->SyncRenderState(...)`
- keep `SubmitCarRender(...)`
- replace only how the data is assembled and passed

### Step 3 - reduce ad hoc visual spread

After repeated stable emulator runs:

- replace direct local visual assembly with packet-driven helpers
- keep submission and actual renderer calls on the Master SH2

## Sector-neutral replacement order

### Replacement A - remove duplicated render assembly first

Candidate removals from `src/game_loop_system.hpp`:

1. inline arithmetic in `ApplyCarCameraDepthBias(...)`
2. inline lift logic in `ApplyCarVisualLift(...)`
3. duplicated `CarRenderFrameState` filling in `BuildCarRenderFrameState(...)`

Only after those removals:

- introduce the equivalent packet assembly in the same patch

Measured result so far:

- replacing only `ApplyCarCameraDepthBias(...)` and `ApplyCarVisualLift(...)`
  with passive assembler calls is binary-budget safe
- replacing `BuildCarRenderFrameState(...)` with local `FrameContext` +
  `SeedRenderPacket(...)` is not binary-budget safe on its own
- that attempt also pushed the ISO to `4136960`

Conclusion:

- the next runtime substitution cannot introduce local render packet assembly in
  `BuildCarRenderFrameState(...)` unless the same patch removes more equivalent
  logic from the hot path

### Replacement B - collapse shadow preparation into packet-driven data

Keep:

- current blob/model draw functions

Replace first:

- their local shadow positioning inputs
- their blob/model mode decisions

Concrete substitution order:

1. remove the duplicated ground-anchor setup from:
   - `DrawCarShadowBlob(...)`
   - `DrawCarShadowModel(...)`
2. remove the duplicated shadow yaw/debug-state setup from both draw functions
3. introduce one local shadow-data assembly step that feeds both draw paths
4. only later consider a full `ShadowPacket` handoff across the render path

Do not do first:

- packetize blob polygon generation
- packetize Scene2D effect toggles
- move mesh-render submission ownership

Those parts are too runtime-specific for the first binary-budget-safe cut.

Measured result of the first shadow runtime cut:

- unify only:
  - ground anchoring
  - shadow yaw propagation
  - shadow debug-state propagation
- keep unchanged:
  - blob footprint generation
  - Scene2D projection/effects
  - SBA shadow mesh render call

Validation result:

- build stable
- passive header validation stable
- ISO remained exactly `4134912`

Accepted runtime shape:

- `PrepareShadowPose(...)` centralizes the shared shadow setup
- `DrawCarShadowBlob(...)` now consumes prepared shadow pose
- `DrawCarShadowModel(...)` now consumes prepared shadow pose
- `RenderCarShadowIfEnabled(...)` selects the ground-bias variant and dispatches
  to the unchanged draw paths

Current implementation note:

- runtime helper: `src/game_loop_car_shadow_runtime_assembler.hpp`
- the helper now builds one narrow `ShadowPacket` per active draw variant from
  the existing `CarRenderFrameState`
- draw entrypoints and submit ownership remain unchanged

### Replacement C - package submit telemetry without moving ownership

Keep:

- `SubmitCarRender(...)` as the actual renderer entrypoint

Replace first:

- ad hoc face-count bookkeeping
- ad hoc submitted-state bookkeeping

Current assessment:

- not recommended as the next isolated runtime patch
- recommended only after the shadow-prep data path is already explicit enough
  that submit and telemetry can reuse the same packet budget

## Immediate next safe step

The next safe step is not a runtime packet integration.

It is:

1. preserve current runtime calls unchanged
2. document the exact removal-first substitutions
3. only then attempt a same-patch remove+replace change in one tiny slice

For the current `CarRender` state, the best next runtime candidate is:

1. keep `SubmitCarRender(...)` unchanged
2. keep `RenderCar(...)` unchanged
3. prepare the shadow-prep substitution map
4. only then attempt a sector-neutral shadow-data cut that can later share
   budget with submit/telemetry packaging

That substitution map is now documented in:

- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`

That shadow-data cut should target only:

1. ground anchoring
2. shadow yaw propagation
3. debug-state propagation
4. blob/model mode selection

## Guard rails

- do not mix this extraction with audio, scheduler or track changes
- do not add new heap allocations
- do not introduce N-1 visual reuse yet
- validate every step with `tools/validate_saturn_stable_build.ps1`
- keep ISO exactly `4134912`

## Recommended next runtime move

The first safe runtime move for car visuals is:

- assemble `CarVisualFramePacket` locally in `RenderCar(...)`
- keep existing shadow draw and submit calls untouched
- do not alter `CarSystem` ownership or APIs in the same patch

## New passive render aggregation

The car visual slice now also participates in one higher-level passive render bundle:

- `src/game_loop_render_debug_contracts.hpp`
- `src/game_loop_render_debug_assembler.hpp`

Current effect:

- `CarVisualFramePacket` can now be grouped off-path with `TrackRenderFramePacket`
- this prepares a future render/presenter facade without touching `RenderCar(...)`
- shadow, submit and mesh-render ownership remain untouched

An additional passive derived-debug slice is now also available for the car path:

- `src/game_loop_car_visual_debug_contracts.hpp`
- `src/game_loop_car_visual_debug_assembler.hpp`

Current effect:

- `CarVisualFramePacket` can now be reduced off-path into a smaller
  `CarVisualDebugPacket`
- future presenter/debug formatting can consume car visual state without walking
  the full render/shadow/submit packet structure
- runtime render ownership remains untouched
