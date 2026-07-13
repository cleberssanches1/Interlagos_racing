# Car Render Boundary Closure Consolidated

## Objective

Consolidate the real current state of the `car render` boundary, explicitly
distinguishing what is already packetized/passive from what is still blocked
from active runtime integration.

This document is inventory-only.

It does not authorize a live runtime move by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the live owner of `RenderCar(...)`
- `SubmitCarRender(...)` remains Master-side
- shadow draw ordering remains unchanged

## Current boundary status

Status:

- Passive/documental

Reason:

- the passive packet/assembler graph already exists
- but broader live `CarVisualFramePacket` integration previously exceeded the
  fixed ISO envelope
- accepted progress so far is limited to narrower shadow-prep and debug-facing
  passive preparation

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
2. `BuildCarRenderFrameState(...)`
3. `RenderCarShadowIfEnabled(...)`
4. `car->SyncRenderState(...)`
5. `SubmitCarRender(...)`

Current active shadow slice:

1. `DrawCarShadowBlob(...)`
2. `DrawCarShadowModel(...)`
3. `StoreShadowDebugState(...)`

## What is already narrower than before

Accepted progress already achieved:

- passive `CarVisualFramePacket` shape is explicit
- passive `CarVisualDebugPacket` shape is explicit
- shadow-prep substitution map is explicit
- render debug aggregation already accepts the car visual slice off-path
- accepted shadow-prep runtime narrowing proved that shared shadow setup can be
  reduced without moving draw ownership

## What still blocks active closure

The boundary is not yet `Active consolidated` because these points remain true:

- `RenderCar(...)` still owns final runtime assembly
- `SubmitCarRender(...)` still owns final submission
- shadow draw entry points still remain in the host
- the broader packet handoff into the live path previously pushed the ISO from
  `4134912` to `4136960`

That means the missing step is not more packet design.

The missing step is a binary-neutral remove-first runtime substitution.

## Current safest live retry shape

If `car render` is reopened, the next acceptable move should target only:

1. shared shadow grounding
2. shadow yaw propagation
3. shadow debug-state propagation
4. blob/model mode selection

That retry must:

- keep `RenderCar(...)` as owner
- keep `DrawCarShadowBlob(...)` and `DrawCarShadowModel(...)` as draw entry
  points
- keep `SubmitCarRender(...)` unchanged
- remove equivalent host logic in the same patch

## What stays outside this boundary

The following concerns still remain intentionally outside any active
consolidated car-render boundary:

- final render submission ownership
- `MeshRenderer` ownership
- shadow draw ordering/effects
- broader runtime packet handoff in `RenderCar(...)`
- any attempt to merge car render with scheduler/reuse or audio

## Why this subsystem is “not closed yet”

Unlike `track-render`, `memory`, and `bootstrap/CD`, this subsystem still lacks:

- a stable active packet consumer boundary in the live host path
- a proven direct replacement path that is sector-neutral at `4134912`

So the correct closure label today is:

- structurally prepared
- runtime-constrained
- not yet actively consolidated

## Recommended next move

Do next:

1. keep the passive graph stable
2. treat `CAR_RENDER_SHADOW_PREP_SUBSTITUTION_MAP.md` as the real next live
   retry map
3. avoid broad `CarVisualFramePacket` runtime integration until an equivalent
   removal-first patch exists

Do not do next:

- introduce the full frame packet in `RenderCar(...)` again without paired
  removals
- mix car render runtime work with another subsystem
- widen the submit path in the first retry
