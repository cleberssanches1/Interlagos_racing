# Debug / Telemetry Size Reduction Plan

## Objective

Recover enough code-size budget in the debug / telemetry path to allow the next
live retry of `Boundary D` (`ReuseObservabilityPacket`) without breaking the
stable Saturn image size baseline.

## Measured constraint

Stable baseline:

- ISO must remain exactly `4134912`

Observed during the first live retry of `Boundary D`:

- runtime logic compiled
- emulator-facing runtime path was kept narrow
- final ISO became `4139008`
- delta versus baseline was exactly `4096` bytes

That means the next live retry needs at least `4 KB` of code-size recovery
before it is attempted again.

## Scope

In scope:

- debug-only presenter helpers
- telemetry string formatting paths
- duplicate compile-only/live-adjacent presenter glue
- packet adaptation layers used only for diagnostics

Out of scope:

- gameplay logic
- simulation scheduling behavior
- track producer behavior
- audio / render runtime policy
- ownership changes in `src/game_loop_system.hpp`

## Hard rules

Any reduction step must preserve:

- ISO `4134912` before reattempting live `Boundary D`
- stable emulator boot
- no invalid opcode
- no silent close
- no scheduling behavior change

## Size-reduction strategy

### Phase 1 - remove duplicate presenter glue first

Primary target:

- redundant helper layers that wrap other helper layers with no behavioral gain

Candidates:

- `src/game_loop_reuse_observability_debug_bundle_presenter_ops.hpp`
- historical first-pass target:
  - removed `src/game_loop_reuse_observability_debug_local_ops.hpp`

Goal:

- keep only one canonical presenter entry per boundary shape
- avoid stacking `packet -> bundle -> local helper -> presenter` when one layer
  is enough

Expected benefit:

- low risk
- small to medium code-size recovery

Current status:

- first reduction patch applied
- duplicate packet-overload forwarding was removed from:
  - `src/game_loop_reuse_observability_debug_bundle_presenter_ops.hpp`
  - removed `src/game_loop_reuse_observability_debug_local_ops.hpp`
- canonical compile-only path is now bundle-first instead of packet-to-bundle
  forwarding through multiple wrappers

### Phase 2 - compress debug text payloads

Primary target:

- long debug format strings and multi-line presenter splits

Candidates:

- `src/game_loop_reuse_observability_debug_presenter_ops.hpp`
- other debug-only presenter files with repeated labels and verbose line names

Goal:

- shorten strings
- merge adjacent lines where runtime readability remains acceptable
- avoid multiple tiny wrappers that only print one line each

Expected benefit:

- low risk
- often good byte recovery because string literals are expensive in this build

Current status:

- second reduction patch applied
- reuse observability presenter was collapsed from two printed lines to one
  compact line in:
  - `src/game_loop_reuse_observability_debug_presenter_ops.hpp`
- long labels like `RSIM` / `RTRK` were replaced by one shorter `RU` line
- follow-up compact patch applied in additional live debug presenters:
  - `src/game_loop_memory_debug_presenter_ops.hpp`
  - `src/game_loop_low_work_overlay_presenter_ops.hpp`
  - `src/game_loop_track_render_presentation_observability_presenter_ops.hpp`
- the follow-up also removed two packet-only forwarding wrappers from
  `src/game_loop_memory_debug_presenter_ops.hpp`
- another host-loop reduction patch removed duplicated `GH3/GH4` and `GLW1-5`
  string payloads from `src/game_loop_system.hpp` by reusing the canonical
  presenter helpers already compiled for that boundary
- an additional track-runtime debug compaction patch shortened the active SH2,
  runtime resource, release, and slide-trace labels in `src/track_system.cxx`
  without changing the diagnostic flow
- a follow-up track debug compaction patch shortened VDP1, SMAP/TGA, family,
  invalid-window, and family-working-set overlays in `src/track_system.cxx`
- another track debug reduction patch shortened repeated `SDR/RDR/TRKRDR/PAK`
  failure labels and compacted the `SEG001` diagnostics in
  `src/track_system.cxx`
- a residual `CMP/S1` compaction pass shortened the remaining `SEG001`,
  renderer-map, texture, overlay, and fallback labels in
  `src/track_system.cxx`
- a follow-up cleanup shortened initial track diagnostics in
  `src/track_system.cxx` and the repeated cart-copy failure strings in
  `src/resource_loader.hpp`
- a final residue pass shortened the remaining `SDR ok...` form in
  `src/track_system.cxx` and compacted the remaining track-load status labels
  in `src/resource_loader.hpp`
- a structural presenter-boundary cleanup removed passive bridge/decision
  overload wrappers from:
  - `src/game_loop_presenter_facade_bridge_assembler.hpp`
  - `src/game_loop_presenter_facade_decision_bridge_assembler.hpp`
  - `src/game_loop_presenter_frame_end_decision_assembler.hpp`
  - `src/game_loop_presenter_hud_telemetry_decision_assembler.hpp`
- the compile-only presenter preview path was kept valid by switching
  `src/game_loop_presenter_compile_only_preview_assembler.hpp` to explicit
  packet construction instead of the removed wrapper chain

### Phase 3 - prefer compile-only aggregation over live helper layering

Primary target:

- helper stacks introduced only to stage future live retries

Goal:

- keep deeper layering compile-only
- do not make runtime consume the full helper stack
- when live retry returns, consume the narrowest already-proven packet directly

Expected benefit:

- low risk
- avoids pulling multiple helper bodies into the live binary

Current status:

- third reduction patch applied
- redundant compile-only local helper layer was removed:
  - deleted `src/game_loop_reuse_observability_debug_local_ops.hpp`
- canonical future retry path now ends at:
  - `src/game_loop_reuse_observability_debug_bundle_presenter_ops.hpp`

### Phase 4 - narrow the future live retry payload

Primary target:

- retry shape for `Boundary D`

Goal:

- do not reintroduce cumulative telemetry fields on the first live retry
- prefer decision-first live retry:
  - `SimulationReuseDecisionViewPacket`
  - `TrackReuseDecisionViewPacket`
- keep telemetry counters compile-only until size headroom is proven

Expected benefit:

- medium risk reduction
- largest likely live-size saving

Current status:

- decision-first live retry was reattempted after Phases 1-3
- cumulative telemetry stayed out of the runtime path
- stable ISO still moved from `4134912` to `4139008`
- the exact blocker therefore remains `4096` bytes even after the first three
  reduction passes

## Recommended execution order

1. measure and simplify presenter/helper duplication in reuse observability
2. shorten reuse debug strings
3. revalidate stable ISO after each tiny reduction patch
4. stop only when at least `4096` bytes of headroom have been recovered
5. only then reattempt live `Boundary D`

## Acceptance gate before reopening Boundary D

Do not reopen the live retry until all are true:

- stable build passes
- ISO is exactly `4134912`
- at least one prior size-reduction patch landed cleanly
- the chosen live retry shape consumes the minimum runtime helper stack
- the retry avoids cumulative telemetry counters on its first return

## Best next patch

The safest next reduction patch is:

1. audit the reuse observability presenter/helper stack
2. collapse duplicate wrapper layers where one helper only forwards to another
3. shorten debug labels in reuse presenter strings
4. validate the ISO after that patch alone
