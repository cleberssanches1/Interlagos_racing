# Car Render Branch Final Status

## Objective

Record the final accepted branch-level state for `car render`, distinguishing
the live accepted narrow seam from the broader runtime handoff that remains
deferred by binary budget.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the runtime owner of `RenderCar(...)`
- final car render submission remains Master-side
- shadow draw ordering remains unchanged

## Accepted active seam for this branch

The accepted live `car render` result for this branch is a narrow render-packet
plus shadow-prep plus submit-telemetry seam only.

Active live path:

- `GameLoopRuntime::BuildCarRenderRuntimePacket(...)`
- `GameLoopRuntime::BuildCarShadowRuntimeDecision(...)`
- `Game::CarRenderSystem::RenderPacket`
- `src/game_loop_car_shadow_runtime_assembler.hpp`
- `GameLoopRuntime::BuildCarShadowPrepPacket(...)`
- `GameLoopRuntime::ApplyCarRenderRuntimeSync(...)`
- `GameLoopRuntime::SubmitCarRenderRuntime(...)`
- `Game::CarRenderSystem::ShadowPacket`
- `Game::CarRenderSystem::Telemetry`
- `DrawCarShadowBlob(...)`
- `DrawCarShadowModel(...)`

Accepted host posture:

- `RenderCar(...)` stays host-owned
- draw entrypoints stay host-owned
- submit ownership stays unchanged
- render-position/yaw packet assembly is localized
- shared shadow preparation is centralized
- shadow mode/bias decision is localized
- visual sync handoff is localized
- submit + face-count telemetry handoff is localized
- the live path no longer depends on `CarRenderFrameState`

## Why this is considered final enough

This branch already proved one stable remove-first runtime cut that met the
real constraints:

- stable emulator startup
- no invalid opcode
- fixed ISO envelope
- no widened renderer ownership

That cut removed duplicated shadow-prep work while preserving the live draw and
submit path.

The latest accepted cleanup inside that same seam also keeps the shadow path
clearer:

- shadow debug capture is explicit at the draw entrypoints
- `BuildShadowDrawYaw(...)` no longer mutates debug state
- draw ownership and submit ownership remain unchanged

One additional cleanup is now also accepted inside the same seam:

- `RenderCar(...)` resolves render inputs through one narrow helper
- runtime policy for depth bias is no longer spread inline at the host call site
- gameplay yaw / visual yaw offset / runtime debug capture are grouped before
  packet assembly
- submission ownership and shadow dispatch ownership remain unchanged

The shadow dispatch seam is also flatter now:

- the runtime helper owns the `enabled -> prepared shadow packet` bridge
- `RenderCarShadowIfEnabled(...)` now consumes that bridge and dispatches only
  the active blob/model draw path
- blob/model draw entrypoints remain host-owned and unchanged

The submit seam is also cleaner now:

- face-count capture is explicit apart from submit execution
- telemetry shaping is explicit apart from face-count capture
- final pipeline reset / submit / flush ownership remains on the Master-side

The SH2-side car-prepare branch is now considered formally frozen:

- `CarRenderPrepareTask` only normalizes yaw
- `CarPrepareRuntimeState.outputYaw` has no live consumer
- the runtime flag for this path is already disabled by default
- no removal-first runtime cut is required there for branch closure

## What remains intentionally outside the active seam

The following are explicitly deferred and are not required for branch closure:

- broad `CarVisualFramePacket` live integration
- final render submission ownership migration
- `MeshRenderer` ownership migration
- broader render/submit packet handoff in `RenderCar(...)`
- combined render/audio/scheduler work

## Deferred rationale

The broader packet handoff remains constrained by measured binary growth under
the fixed ISO envelope:

- `CAR_RENDER_BOUNDARY_CLOSURE_CONSOLIDATED.md`
- `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md`

## Branch-final interpretation

For the purpose of refactor closure in this branch:

- `car render` is considered actively consolidated at a narrow runtime seam
- broader visual/runtime integration is formally deferred
- no broader `CarVisualFramePacket` live retry is required before branch
  closure

## What can still happen later

Future work may still reopen `car render`, but only as a new explicit runtime
goal, not as part of the remaining mandatory refactor closure for this branch.
