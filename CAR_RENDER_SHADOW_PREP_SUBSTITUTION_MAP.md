# Car Render Shadow Prep Substitution Map

## Objective

Describe the smallest remove-first runtime substitution that can advance the
`CarRender` path without changing:

- render ownership
- shadow draw ordering
- Master SH2 submission
- binary envelope stability

This document is preparatory.

It does not authorize a broad `CarVisualFramePacket` integration by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the runtime owner of `RenderCar(...)`
- `DrawCarShadowBlob(...)` and `DrawCarShadowModel(...)` remain the draw entrypoints
- `SubmitCarRender(...)` remains unchanged in the first cut

## Current runtime slice

Current shadow-prep work still split across the host:

1. `RenderCarShadowIfEnabled(...)`
2. `DrawCarShadowBlob(...)`
3. `DrawCarShadowModel(...)`
4. `StoreShadowDebugState(...)`

Shared data currently prepared ad hoc:

- anchored shadow world position
- shadow yaw
- shadow debug state
- blob/model enable flags

## Passive coverage already available

Existing passive helpers already cover the shared data shape:

- `CarRenderDomain::SeedShadowPacket(...)`
- `CarRenderDomain::AnchorShadowToGround(...)`
- `CarRenderDomain::ConfigureShadowModes(...)`
- `Game::CarRenderSystem::BuildShadowPacket(...)`

New compile-only preview above that seam:

- `src/game_loop_car_shadow_prep_preview_contracts.hpp`
- `src/game_loop_car_shadow_prep_preview_assembler.hpp`

Current preview payload:

- `Game::CarRenderSystem::RenderPacket`
- `Game::CarRenderSystem::ShadowPacket`

## Remove-first substitution target

The first safe runtime substitution should remove duplicated setup first and
only then consume the passive shadow packet shape in the freed budget.

### Remove first

From `DrawCarShadowBlob(...)`:

- local ground-anchor rewrite of `center.Y`
- local `shadowYawDeg` extraction
- direct `StoreShadowDebugState(...)` input assembly

From `DrawCarShadowModel(...)`:

- local ground-anchor rewrite of `shadowPos.Y`
- local `shadowYawDeg` extraction
- direct `StoreShadowDebugState(...)` input assembly

### Keep unchanged in the first runtime cut

- blob polygon footprint generation
- projection to 2D
- Scene2D effect toggles
- SBA shadow mesh render call
- final `RenderCarShadowIfEnabled(...)` dispatch ownership

## Target runtime shape

The first accepted runtime shape should be:

1. one local shadow-prep assembly step
2. both draw paths consume prepared shadow position/yaw
3. draw calls remain where they are
4. submit path remains untouched

Concretely:

- `RenderCarShadowIfEnabled(...)` decides blob/model path and ground-bias variant
- one narrow shadow packet or equivalent prepared pose is assembled once
- `DrawCarShadowBlob(...)` consumes the prepared pose
- `DrawCarShadowModel(...)` consumes the prepared pose

## Why this is the right next cut

This slice is better than a full `CarVisualFramePacket` runtime integration
because:

- it removes duplicated host logic
- it does not move renderer ownership
- it does not widen the submit path
- it can pay for itself in binary budget more easily than a broader packet handoff

## Do next

1. keep this map runtime-neutral
2. use the preview packet as the compile-only guard rail
3. attempt only one remove+replace shadow-prep cut in a future runtime patch
4. validate immediately against ISO and emulator boot

## Do not do next

- integrate full `CarVisualFramePacket` into `RenderCar(...)` first
- move `SubmitCarRender(...)` together with this cut
- mix this cut with scheduler/reuse or audio work
- introduce new persistent members or heap allocations
