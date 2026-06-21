# Auto Lap Route Passive Flow Plan

## Objective

Prepare the extraction of AutoLap route coordination out of `src/game_loop_system.hpp`
without changing the current runtime path.

## Current passive building blocks

- `src/auto_lap_route_contracts.hpp`
- `src/auto_lap_route_state_assembler.hpp`
- `src/auto_lap_route_transition_ops.hpp`
- `src/auto_lap_route_build_ops.hpp`
- `src/auto_lap_route_lifecycle_ops.hpp`
- `src/auto_lap_route_runtime_state.hpp`
- `src/game_loop_auto_lap_packet.hpp`
- `src/game_loop_auto_lap_packet_assembler.hpp`

These files already describe a passive AutoLap path for:

- frame-context assembly
- storage snapshot assembly
- guide-load state assembly
- route-build state assembly
- route-step state assembly
- frame-local aggregation of AutoLap coordination state

## Binary-budget constraint

The first runtime experiment for this slice already proved a hard constraint:

- even a minimal debug-only `AutoLapFramePacket` integration inside
  `src/game_loop_system.hpp` pushed the ISO from `4134912` to `4136960`
- therefore the next AutoLap runtime step cannot be additive
- the next runtime step must be substitutional:
  - remove equivalent local state assembly first
  - only then consume the passive packet in the freed budget

This means the current passive work remains valid, but the next runtime cut
must avoid introducing new AutoLap packet assembly on top of existing logic.

One safe substitution already validated for this slice is:

- remove thin AutoLap wrappers from `src/game_loop_system.hpp`
- call `AutoLapRouteDomain::*` lifecycle helpers directly at the use sites

This is acceptable because it reduces local code instead of layering new
packet/runtime assembly on top of the existing flow.

Another safe substitution already validated for this slice is:

- move build-reset and guide-build finalization helpers into
  `src/auto_lap_route_lifecycle_ops.hpp`
- remove the single-use local wrappers from `src/game_loop_system.hpp`

This keeps the route-build flow unchanged while shrinking local orchestration
code in the critical loop host.

Another safe substitution already validated for this slice is:

- move pure route-search helpers into `src/auto_lap_route_build_ops.hpp`
- remove the corresponding local methods from `src/game_loop_system.hpp`

Validated examples:

- nearest route-point search
- best guide-line selection

These are good candidates because they depend only on AutoLap state plus
explicit input values, so they reduce host-local algorithmic code without
changing ownership or execution order.

Another safe substitution already validated for this slice is:

- move additional route-advance helpers into `src/auto_lap_route_build_ops.hpp`
- remove their local equivalents from `src/game_loop_system.hpp`

Validated examples:

- segment-id wrapping
- route-direction scoring
- waypoint reached/passed test

These remain safe because they are deterministic calculations over explicit
inputs and do not change route ownership or frame sequencing.

Another safe substitution already validated for this slice is:

- move the heading-vector assembly into `src/auto_lap_route_build_ops.hpp`
- keep only final yaw/offset application in `src/game_loop_system.hpp`

This is a good split because it removes route-lookahead vector assembly from
the critical host while preserving the final yaw normalization and state write
at the consumption point.

Another safe substitution already validated for this slice is:

- move guide-line segment mapping, observed-segment advance and ground-Y
  resolution helpers into `src/auto_lap_route_build_ops.hpp`
- keep the host only as the call-site orchestrator

Validated examples:

- point-to-segment scoring
- best-segment search per route point
- route population from guide line
- route direction normalization
- observed segment advance
- route ground-Y resolution

This removes most of the remaining AutoLap algorithmic/helpers from the host
without changing route ownership or frame ordering.

Another safe substitution already validated for this slice is:

- move guide-line simplification/selection helpers and fallback-center
  population into `src/auto_lap_route_build_ops.hpp`
- keep only the remaining orchestration and final yaw rebuild in the host

This leaves the host with a much smaller AutoLap surface focused on:

- guide load orchestration
- final yaw rebuild/state write
- final fallback/build orchestration

Another safe substitution already validated for this slice is:

- move guide-path candidate/read/parse/copy mechanics into
  `src/auto_lap_route_build_ops.hpp`
- remove thin local forwarding wrappers from `src/game_loop_system.hpp`

This keeps logging and final orchestration local, but further reduces host-local
algorithmic and asset-loading detail without changing AutoLap ownership.

## Current runtime touch points

The AutoLap domain is still consumed directly in `src/game_loop_system.hpp`,
mainly for:

1. route readiness and initialization
2. yaw rebuild and heading update
3. guide loading/parsing/copy
4. route building from guide or fallback track segments
5. stepping/waypoint advance
6. storage release/reset

This means the domain already has passive helpers, but the runtime still owns:

- coordination order
- many direct reads/writes to `autoLapRoute_`
- ad hoc grouping of build/init/step state

## Passive packet model

`src/game_loop_auto_lap_packet.hpp` aggregates:

- `AutoLapFrameContext`
- `AutoLapRouteStorageSnapshot`
- `AutoLapGuideLoadPacket`
- `AutoLapRouteBuildPacket`
- `AutoLapRouteStepPacket`

This stays:

- frame-local
- non-owning
- runtime-neutral until explicit integration is needed

`src/game_loop_auto_lap_packet_assembler.hpp` now also provides passive
builder entry points for `AutoLapFramePacket`, so future substitutional runtime
cuts can assemble the full packet from already-passive route contracts without
reintroducing ad hoc packet wiring in the host.

The passive contract layer now also includes an `AutoLapGuideRouteTrace`
builder path, so the current host-local guide-route selection/fallback logging
can later move behind explicit passive data instead of ad hoc local values.

`src/game_loop_auto_lap_packet.hpp` and
`src/game_loop_auto_lap_packet_assembler.hpp` now carry that guide-route trace
through the passive frame packet as well, keeping future substitutional runtime
cuts aligned around one explicit AutoLap packet shape.

The guide-load passive side now also has explicit builders for:

- read failure
- parse failure
- parse success from `PathNya::ParseResult`

This keeps the remaining host-local guide-load flow easier to substitute later
without re-encoding packet values at the call site.

## Runtime-to-passive substitution map

### Frame context

Passive coverage already available:

1. `AutoLapRouteDomain::BuildAutoLapFrameContext(...)`
2. `AutoLapRouteDomain::SeedAutoLapFrameContext(...)`

Main source data that will eventually feed it:

- AutoLap enabled flag
- track ready state
- step units
- latest active segment id
- track segment offset
- reference car world position
- current car world position
- current car yaw

### Storage snapshot

Passive coverage already available:

1. `AutoLapRouteDomain::BuildAutoLapRouteStorageSnapshot(...)`
2. `AutoLapRouteDomain::SeedAutoLapRouteStorageSnapshot(...)`

Current runtime-relevant outputs:

- initialized/built/startup-yaw flags
- current route index
- route buffer counts
- base/current yaw offsets
- selected guide line
- guide line point counts

### Guide/build/step packets

Passive coverage already available:

1. `AutoLapRouteDomain::BuildAutoLapGuideLoadPacket(...)`
2. `AutoLapRouteDomain::BuildAutoLapRouteBuildPacket(...)`
3. `AutoLapRouteDomain::BuildAutoLapRouteStepPacket(...)`

Current runtime-relevant outputs:

- guide load candidate/result state
- selected guide line and route counts
- observed segment id
- route index
- step position/yaw snapshot

## Safe integration order

### Step 1 - keep runtime behavior unchanged

Do not change:

- AutoLap enable/disable flow
- route build fallback ordering
- waypoint stepping rules
- current `autoLapRoute_` ownership

### Step 2 - packetize local AutoLap state only

When runtime integration becomes safe, assemble locally:

1. `AutoLapFrameContext`
2. `AutoLapRouteStorageSnapshot`
3. `AutoLapGuideLoadPacket`
4. `AutoLapRouteBuildPacket`
5. `AutoLapRouteStepPacket`
6. `AutoLapFramePacket`

Consume immediately in the same scope.

No new persistent members.
No behavior changes.
No route-storage ownership changes.

### Step 3 - replace repeated passive reads first

The best first runtime candidate is not route stepping itself.

It is:

- unify local AutoLap diagnostics/debug snapshots
- centralize guide/build/step state handoff
- reduce repeated ad hoc state grouping around `autoLapRoute_`

This is lower risk than changing build/step ordering.

### Step 4 - only later isolate coordination

Only after repeated stable emulator runs:

- move build/init/step orchestration behind explicit local packets
- reduce direct `autoLapRoute_` spread in `src/game_loop_system.hpp`
- then consider a true AutoLap coordination facade

## Guard rails

- do not change AutoLap stepping semantics in the same patch
- do not mix this with track/audio runtime cuts
- validate every step with `tools/validate_saturn_stable_build.ps1`
- keep ISO exactly `4134912`

## Immediate next safe step

The next safe step is:

1. keep runtime behavior unchanged
2. use this packet only as passive groundwork
3. later try a tiny local cut that reuses one AutoLap packet for debug/trace
   assembly before touching route control flow

## Validation coverage

Current compile-only SH2 validation now covers:

- `src/game_loop_auto_lap_packet.hpp`
- `src/game_loop_auto_lap_packet_assembler.hpp`

through:

- `tools/validate_game_loop_passive_headers.ps1`

A safe host-side substitution now also validated for this slice is:

- replace ad hoc guide-route log arguments in `src/game_loop_system.hpp` with
  `AutoLapGuideRouteTrace`
- keep the same logging behavior and call order

This is a small host/runtime substitution that consumes the already-passive route trace without touching guide-load orchestration or yaw rebuild.

