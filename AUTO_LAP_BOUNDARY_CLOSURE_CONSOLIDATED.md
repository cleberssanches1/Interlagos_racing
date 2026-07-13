# AutoLap Boundary Closure Consolidated

## Objective

Consolidate the real current state of the `AutoLap` boundary, explicitly
distinguishing what is already packetized/passive from what is still host-owned
or deferred from active runtime boundary closure.

This document is inventory-only.

It does not authorize a live runtime move by itself.

## Stable baseline

- ISO must remain exactly `4134912`
- emulator boot must remain stable
- `src/game_loop_system.hpp` remains the live owner of the AutoLap runtime path
- route build timing and route stepping ownership remain unchanged

## Current boundary status

Status:

- Passive/documental

Reason:

- the route state, build, lifecycle, and packet slices are already explicit
- but the live route ownership and runtime stepping still remain inside the host
- no active boundary has yet replaced the current host-owned AutoLap flow

## Passive contract families already closed

### 1. AutoLap core contract family

Files:

- `src/auto_lap_route_contracts.hpp`
- `src/auto_lap_route_runtime_state.hpp`
- `src/auto_lap_route_state_assembler.hpp`

Role:

- define frame context
- define route storage snapshots
- define guide-load, route-build, guide-route-trace, and route-step packets
- preserve the current route-state shape outside the live host path

### 2. AutoLap lifecycle/reset family

Files:

- `src/auto_lap_route_lifecycle_ops.hpp`

Role:

- reset route flags and scalars
- clear/release retained route storage
- finalize guide/fallback build state
- isolate state lifecycle transitions from the host call sites

### 3. AutoLap route build family

Files:

- `src/auto_lap_route_build_ops.hpp`

Role:

- validate route state
- score and map guide-line points to segments
- populate route points from guide lines
- normalize route direction
- advance observed segment state
- resolve route ground Y

### 4. AutoLap packet/transition family

Files:

- `src/auto_lap_route_transition_ops.hpp`
- `src/game_loop_auto_lap_packet.hpp`
- `src/game_loop_auto_lap_packet_assembler.hpp`

Role:

- build frame-local AutoLap packet slices
- preserve explicit transition-level packetization above route state/build data
- prepare future narrow substitution points without moving ownership yet

## Current live runtime touch points

The real live AutoLap path still belongs to `src/game_loop_system.hpp`.

Current live host-owned concerns include:

- AutoLap enable/disable cadence
- guide loading timing
- route rebuild timing
- route stepping and current index ownership
- yaw alignment timing
- integration with track and car runtime state

In practice, the passive `AutoLap` helpers already describe the route domain
well, but the host still owns when and how that domain is executed.

## What is already narrower than before

Accepted progress already achieved:

- route storage state is explicit
- guide-load state is explicit
- route-build success/fallback state is explicit
- route-step state is explicit
- lifecycle/reset/release logic is externalized
- guide-line mapping and segment scoring are externalized
- observed-segment advancement and ground-Y resolution are externalized

## What still blocks active closure

The boundary is not yet `Active consolidated` because these points remain true:

- the host still owns execution timing of the AutoLap route
- the host still owns route/current-index progression
- the host still owns the decision of when to load, rebuild, reset, or fallback
- there is no proven narrow live packet consumer replacing that host path yet

This means the missing step is not more packet slicing.

The missing step is a remove-first runtime substitution that replaces one
host-owned execution slice without widening route ownership.

## Current safest future live retry shape

If `AutoLap` is reopened for runtime substitution, the next acceptable move
should target only one narrow slice such as:

1. guide-load result consumption
2. route-build result consumption
3. route-step read-only observability

That retry must:

- keep host ownership of the overall AutoLap flow
- keep route storage ownership local
- avoid mixing track, camera, or gameplay runtime ownership changes
- remove an equivalent host-local branch in the same patch

## What stays outside this boundary

The following concerns still remain intentionally outside any active
consolidated AutoLap boundary:

- final runtime route ownership
- route-step mutation ownership
- integration timing with track/car runtime state
- broader gameplay/AI ownership

## Why this subsystem is “not closed yet”

Unlike `track-render`, `memory`, and `bootstrap/CD`, this subsystem still lacks:

- a stable active packet consumer boundary in the live host path
- a proven narrow live substitution that keeps the runtime shape sector-neutral

So the correct closure label today is:

- structurally prepared
- runtime-deferred
- not yet actively consolidated

## Recommended next move

Do next:

1. keep the passive graph stable
2. treat the current AutoLap packet/route helpers as the future substitution
   inventory
3. only reopen runtime through one narrow host-owned slice at a time

Do not do next:

- move the whole AutoLap flow off the host in one patch
- mix AutoLap runtime work with scheduler/audio/bootstrap changes
- widen the first retry into route ownership migration
