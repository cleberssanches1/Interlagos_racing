# Presenter Boundary Decision Retry Blocker

## Objective

Record why the next apparent presenter-side live retry,
`PresenterBoundaryDecisionTextPacket`, is currently blocked by the remove-first
rule.

## Current accepted live state

`PrintDrivingHud()` is now fully localized around `DrivingHudTextPacket`:

- status line at row `0,12`
- shift line at row `0,11`

That retry remained safe because both lines already existed locally and were
replaced in the same patch.

## Why the decision packet is different

`PresenterBoundaryDecisionTextPacket` currently carries:

- `shouldPresentDrivingHud`
- `shouldPresentPeriodicHud`
- `shouldPresentRenderDebug`
- `shouldPresentOverlayDebug`
- `shouldPresentObservability`
- `shouldPresentSchedulerReuseDebug`
- `shouldPresentMemoryDebug`

Those values are valid compile-only exports, but there is no current equivalent
direct debug line in `src/game_loop_system.hpp` that prints this same decision
surface and can be removed in the same patch.

## Concrete blocker

The current presenter-side helper exists:

- `src/game_loop_presenter_boundary_view_presenter_ops.hpp`
  - `PresentPresenterBoundaryDecisionTextPacket(...)`

But there is no matching live host line today in `src/game_loop_system.hpp`
that:

- prints these exact flags
- is already localized to one existing presenter/debug-only line
- can be replaced one-for-one without widening runtime ownership

So a live integration now would be additive, not substitutional.

## What would make it safe later

One of these must become true first:

1. a real equivalent local debug line already exists and can be replaced
2. a new local non-critical presenter/debug line is first introduced for another
   justified reason and then later normalized through the packet
3. another subsystem offers a natural remove-first consumer for these exact
   decisions

Until then, this packet should stay compile-only.

## Current recommendation

Do not wire `PresentPresenterBoundaryDecisionTextPacket(...)` into runtime now.

The next safe options are:

- continue in another passive boundary
- or prepare a future local call-site whose semantics actually match the
  decision packet

## Guard rail

If a future retry cannot remove an equivalent existing line in the same patch,
it must not be treated as a live substitution.
