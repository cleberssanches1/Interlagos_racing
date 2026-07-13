# Car Render Boundary Closure Consolidated

## Objective

Consolidate the real current state of the `car render` boundary, explicitly
distinguishing what is already active in runtime from what remains deferred by
binary budget and ownership sensitivity.

This document is inventory-only.

It does not authorize a broader live runtime move by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the live owner of `RenderCar(...)`
- final car render submission remains Master-side
- shadow draw ordering remains unchanged

## Current boundary status

Status:

- Active consolidated at narrow render/shadow/sync/submit seam, broader branch
  deferred

Reason:

- the passive packet/assembler graph already exists
- a narrow live runtime seam is now accepted in the host
- broader live `CarVisualFramePacket` integration previously exceeded the fixed
  ISO envelope

## Passive contract families already closed

### 1. Car render core packet family

Files:

- `src/car_render_contracts.hpp`
- `src/car_render_state_assembler.hpp`
- `src/car_render_transition_ops.hpp`
- `src/car_render_system.hpp`
- `src/car_render_submitter.hpp`

Role:

- define frame context, render packet, shadow packet, submit packet, telemetry
- preserve the future frame-local render shape outside the live host path

### 2. Car visual frame packet boundary

Files:

- `src/game_loop_car_visual_packet.hpp`
- `src/game_loop_car_visual_packet_assembler.hpp`

Role:

- aggregate:
  - `FrameContext`
  - `RenderPacket`
  - `ShadowPacket`
  - `SubmitPacket`
  - `Telemetry`
  - `syncYawDeg`
  - `car`
- preserve the future frame-local handoff shape for `RenderCar(...)`

### 3. Shadow-prep preview boundary

Files:

- `src/game_loop_car_shadow_prep_preview_contracts.hpp`
- `src/game_loop_car_shadow_prep_preview_assembler.hpp`

Role:

- preserve a compile-only preview directly above render/shadow prep
- keep the future first live cut focused on shared shadow data only

### 4. Car visual debug boundary

Files:

- `src/game_loop_car_visual_debug_contracts.hpp`
- `src/game_loop_car_visual_debug_assembler.hpp`
- `src/game_loop_render_debug_contracts.hpp`
- `src/game_loop_render_debug_assembler.hpp`

Role:

- derive `CarVisualDebugPacket`
- join car visual state into the broader render debug bundle
- keep debug/presenter preparation outside the runtime hot path

## Current live runtime touch points

The real live render path still belongs to `src/game_loop_system.hpp`.

Current active host-owned sequence:

1. `ResolveCarRenderPosition(...)`
2. `GameLoopRuntime::BuildCarRenderRuntimePacket(...)`
3. `GameLoopRuntime::BuildCarShadowRuntimeDecision(...)`
4. `RenderCarShadowIfEnabled(...)`
5. `GameLoopRuntime::ApplyCarRenderRuntimeSync(...)`
6. `GameLoopRuntime::SubmitCarRenderRuntime(...)`

Current active shadow slice:

1. `DrawCarShadowBlob(...)`
2. `DrawCarShadowModel(...)`
3. `StoreShadowDebugState(...)`

## What is already narrower than before

Accepted progress already achieved:

- narrow live `RenderPacket` assembly is explicit
- narrow live shadow decision assembly is explicit
- narrow live visual sync handoff is explicit
- narrow live submit + telemetry handoff is explicit
- shadow-prep live path now consumes `RenderPacket` directly
- the live path no longer depends on `CarRenderFrameState`
- passive `CarVisualFramePacket` shape is explicit
- passive `CarVisualDebugPacket` shape is explicit
- shadow-prep substitution map is explicit
- render debug aggregation already accepts the car visual slice off-path

## What still blocks broader runtime closure

The boundary remains intentionally narrow because these points remain true:

- `RenderCar(...)` still owns final runtime orchestration
- the live submit path still owns final submission on the Master side
- shadow draw entry points still remain in the host
- the broader packet handoff into the live path previously pushed the ISO from
  `4134912` to `4136960`

That means the deferred step is not more packet design.

The deferred step is a broader binary-neutral remove-first runtime substitution.

## Current safest broader retry shape

If `car render` is reopened beyond the accepted seam, the next acceptable move
should target only one narrow remove-first widening at a time.

The best remaining candidate is still the broader packet handoff around:

1. shared shadow grounding
2. shadow yaw propagation
3. shadow debug-state propagation
4. blob/model mode selection
5. eventual broader frame-packet consumption

That retry must:

- keep `RenderCar(...)` as owner
- keep `DrawCarShadowBlob(...)` and `DrawCarShadowModel(...)` as draw entry
  points
- keep the final submit path unchanged at the Master-side ownership boundary
- remove equivalent host logic in the same patch

## What stays outside this boundary

The following concerns still remain intentionally outside any active
consolidated car-render boundary:

- final render submission ownership migration
- `MeshRenderer` ownership migration
- shadow draw ordering/effects
- broader runtime packet handoff in `RenderCar(...)`
- any attempt to merge car render with scheduler/reuse or audio

## Why this subsystem is considered closed enough for the branch

Unlike earlier stages of this refactor, this subsystem now already has:

- a stable active runtime seam in the live host path
- repeated proof that the seam remains sector-neutral at `4134912`
- explicit declaration of what remains outside the seam

So the correct closure label for this branch today is:

- actively consolidated at a narrow seam
- broader runtime work deferred

## Recommended next move

Do next:

1. keep the active narrow seam stable
2. keep the passive graph stable
3. treat `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md` as a future reopening map
   only if a new explicit runtime goal is chosen
4. avoid broad `CarVisualFramePacket` runtime integration in this branch

Do not do next:

- introduce the full frame packet in `RenderCar(...)` again without paired
  removals
- mix car render runtime work with another subsystem
- widen the submit path in the first broader retry
