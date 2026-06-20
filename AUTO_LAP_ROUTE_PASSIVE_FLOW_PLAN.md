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
