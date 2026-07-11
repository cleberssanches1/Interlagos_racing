# Simulation Reuse Live Retry Blocker

## Status

The first live retry that completed the simulation-side branch at the local
reuse seam compiled and preserved the fixed ISO envelope, but the emulator no
longer booted reliably.

That means the blocker is runtime integrity, not passive-contract correctness.

## What changed in the failed retry

The failed retry combined three live moves in one patch inside
`src/game_loop_system.hpp`:

1. commit simulation reuse history at authoritative Slave-output consumption
   points;
2. derive simulation-side reuse decision at the local observability seam;
3. assemble one combined simulation+track runtime owner packet for the same
   seam.

## Why this is the likely fault boundary

The last stable shape before the failure already proved:

- track-only runtime seam is stable;
- passive simulation reuse commit helpers are stable;
- passive simulation reuse decision helpers are stable.

The emulator failure appeared only after the host started consuming the new
simulation-side live path in the critical runtime file.

So the regression is most likely caused by one or more of:

- widened host sequencing in `src/game_loop_system.hpp`;
- extra inline code and stack pressure at the local seam;
- combining representation change and timing change in the same live retry.

## Latest narrowed retry result

After stabilizing the authoritative simulation-history commit separately, the
next narrower retry was attempted as:

1. keep the new simulation-history commit points live;
2. replace only the local track-only owner-packet seam with the combined
   `simulation + track` owner packet;
3. keep the existing local observability consumer unchanged.

Observed result:

- stable build completed;
- final ISO became `4136960`;
- baseline delta was `2048` bytes;
- the retry was reverted immediately to preserve the fixed envelope.

This means the current seam blocker is now narrower and better understood:

- the remaining owner-packet join itself still exceeds the current code-size
  envelope, even after the simulation-history commit has already been split out
  and stabilized.

## Safe conclusion

Do not reopen the simulation-side live seam with the combined owner-packet
retry first.

The next retry must be narrower than:

- `SimulationReuseRuntimeState` live commit
- plus combined `FrameReuseRuntimeOwnerPacket`
- plus local observability consumer change

all in the same patch.

At the current state, even the reduced retry:

- live simulation-history commit
- plus combined owner-packet seam join

is still too large for the fixed ISO envelope.

## Recommended next cut

Prepare and validate a compile-only preview directly above the future live
simulation seam:

- exact local host inputs
- derived `SimulationReuseDecisionViewPacket`
- no `FrameReuseRuntimeOwnerPacket` change
- no live commit point change

That preview now exists as:

- `src/game_loop_simulation_reuse_runtime_preview_contracts.hpp`
- `src/game_loop_simulation_reuse_runtime_preview_assembler.hpp`

## Next live retry shape

When reopening runtime, prefer this order:

1. local preview-only consumption in debug/observability code;
2. only then one authoritative simulation-history commit line;
3. only after repeated stability, join it with the track-side owner packet.

Additional guard rail now required:

4. do not retry the combined owner-packet join again until a separate
   code-size reduction pass recovers at least `2048` bytes of always-live
   budget, or the user explicitly authorizes a different ISO-envelope strategy.
