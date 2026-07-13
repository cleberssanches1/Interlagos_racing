# Game Loop Runtime Critical Engineering Policy

## Objective

Define one explicit engineering policy for runtime-critical work centered on:

- `src/game_loop_system.hpp`
- `src/main.cxx`
- `src/track_system.hpp`
- `src/track_system.cxx`
- `src/car_system.hpp`
- `src/car_system.cxx`

This document exists to stop circular retries and make runtime tradeoffs
explicit before code changes.

## Hardware / toolchain reality

### SH2 instruction reality

- SH2 instructions are fixed-width `16-bit`
- the practical problem is not one "large instruction"
- the practical problem is how many instructions the compiler emits

### C++ method reality

- there is no useful day-to-day C++ source-level "method size limit" for this
  project
- the real limit is generated code size, stack footprint, and runtime layout

### Header / inline reality

- `inline` helpers in `.hpp` files can grow code in multiple call sites
- a source change that looks small can still increase the final Saturn binary
- compile-only helpers are not free if they become live in critical paths

## Project hard limits

### Stable envelope

The current hard runtime envelope is:

- final ISO must remain exactly `4134912`

Any runtime-facing patch that pushes the final ISO above that value is blocked
until equivalent always-live code is removed.

### Critical host sensitivity

The highest-risk runtime host is:

- `src/game_loop_system.hpp`

For this host, small layout shifts can cause:

- emulator boot regressions
- invalid opcode failures
- silent close on startup
- binary-envelope overflow

## Engineering rules

### Rule 1 - remove-first only

For runtime-critical boundaries:

1. add the narrowest packet/helper needed
2. consume it locally in the existing flow
3. remove equivalent local reads/formatting in the same patch

Do not keep additive runtime glue in the critical host.

### Rule 2 - no growth without subtraction

Do not add:

- new helper layers
- new preview layers
- new runtime includes
- new local wrapper methods

inside `src/game_loop_system.hpp` unless the same patch removes equivalent
runtime code.

### Rule 3 - prefer passive over live

Prefer this order:

1. contract
2. pure assembler/helper
3. compile-only consumer
4. local live substitution
5. only later ownership movement

### Rule 4 - stack matters

Large methods and wide local packets are risky because they can increase:

- stack usage
- register pressure
- generated instruction count

So "method size" is evaluated by generated runtime cost, not only by line
count.

### Rule 5 - no speculative seams

If there is no real host-local remove-first consumer, the boundary stays:

- compile-only
- documented
- non-live

Do not force a runtime consumer only to "use" an already prepared packet.

## Accepted measurement rules

### What counts as a valid runtime retry

A retry is acceptable only if all remain true:

- ISO stays `4134912`
- emulator boot stays stable
- no invalid opcode
- no silent close
- no scheduling drift
- no audio/render ownership drift

### What counts as a blocked retry

A retry is blocked if any of these happens:

- ISO grows to `4136960` or above
- ISO grows by `+2048` or `+4096`
- equivalent local runtime code was not removed
- the patch widens `src/game_loop_system.hpp`

## Current measured examples

Already measured in this project:

- some broader scheduler/reuse retries cost `+4096`
- the next narrow retries above accepted `Track Render` and `Scheduler/Reuse`
  seams cost `+2048`

So the current constraint is not only semantics.

It is also binary budget.

## Practical code review checklist

Before accepting a runtime patch in `src/game_loop_system.hpp`, verify:

1. what exact local reads or wrappers were removed
2. whether a new include was added
3. whether a new stack-local aggregate was introduced
4. whether the same effect could stay compile-only
5. whether the stable ISO envelope was preserved

## Validation contract

Use this order:

1. `powershell -ExecutionPolicy Bypass -File tools\\validate_saturn_stable_build.ps1`
2. recreate:
   - `BuildDrop\\passive_header_validation`
   - `BuildDrop\\observability_header_validation`
3. `powershell -ExecutionPolicy Bypass -File tools\\validate_game_loop_passive_headers.ps1`
4. `powershell -ExecutionPolicy Bypass -File tools\\validate_game_loop_observability_headers.ps1`
5. clean transient:
   - `src\\car_wheel_rig.o`
   - `src\\track_pipeline_stages.o`

## How to use this policy

Use this document when deciding:

- whether a new helper should stay compile-only
- whether a runtime seam can be reopened
- whether a method is "too large" in practice
- whether a retry is blocked by semantics or by binary budget

## Related documents

- `REFACTOR_CONSOLIDATED_FINAL_INDEX.md`
- `PASSIVE_CONTRACTS_INVENTORY.md`
- `REFACTOR_RUNTIME_SAFE_NEXT_CUTS.md`
- `SCHEDULER_REUSE_LIVE_INTEGRATION_INVENTORY.md`
- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`
