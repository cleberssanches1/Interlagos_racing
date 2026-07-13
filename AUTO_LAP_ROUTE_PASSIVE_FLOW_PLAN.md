# Auto Lap Route Passive Flow Plan

## Objective

Keep the passive `AutoLap` graph documented and stable after branch closure.

## Passive building blocks

- `src/auto_lap_route_contracts.hpp`
- `src/auto_lap_route_state_assembler.hpp`
- `src/auto_lap_route_transition_ops.hpp`
- `src/auto_lap_route_build_ops.hpp`
- `src/auto_lap_route_lifecycle_ops.hpp`
- `src/auto_lap_route_runtime_state.hpp`
- `src/game_loop_auto_lap_packet.hpp`
- `src/game_loop_auto_lap_packet_assembler.hpp`

## Accepted live seam

The accepted branch seam remains:

- lifecycle helpers consumed from the host
- route-build/search helpers consumed from the host
- `AutoLapGuideRouteTrace` used for route-selection logging

## Binary-budget constraint

The wider runtime attempt proved the hard limit again:

- stable ISO: `4134912`
- wider AutoLap runtime attempt: `4136960`

Therefore:

- the passive packet family stays valid
- broader live packet/runtime handoff is frozen for this branch

## Branch-final decision

For this branch:

1. keep runtime behavior unchanged
2. keep the packet family as passive groundwork
3. treat broader live AutoLap packet integration as out of scope

## Reopen rule

Any broader `AutoLap` runtime move now requires a new explicit goal.
