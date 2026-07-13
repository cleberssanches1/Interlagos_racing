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
- a map-driven `track_system` cut removed the host-only local segments-map
  fallback from `src/track_system.cxx` behind
  `TRACK_ENABLE_HOST_SEGMENTS_MAP_FALLBACK=0`, which dropped
  `src/track_system.o` `.text` from `0x31678` to `0x31034`
- a follow-up CD/TGA reader cleanup in `src/track_system.cxx` removed the
  duplicated chunked read loops in `ReadCdFileText(...)` and
  `ReadCdFileBinary(...)` by reusing `ReadCdFileFully(...)`, which dropped
  `src/track_system.o` `.text` again from `0x31034` to `0x30e94`
- a follow-up preload cleanup split repeated reset, SMAP snapshot, last-name,
  catalog-check, and finalize blocks out of
  `TrackSystem::PreloadTgaCatalogFromSegmentsMap()` in `src/track_system.cxx`,
  dropping `src/track_system.o` `.text` again from `0x30e94` to `0x30db0`
- a follow-up TGA cart-load cleanup replaced the inline candidate-path
  construction and inline CD-to-cart read block inside
  `TrackSystem::PreloadTgaCatalogFromSegmentsMap()` with shared helpers in
  `src/track_system.cxx`, dropping `src/track_system.o` `.text` again from
  `0x30db0` to `0x30b04`
- a follow-up consolidation pass extracted the fallback `.tga` token scan and
  the repeated `SMAP/RTMAP` text-load call sites into shared helpers in
  `src/track_system.cxx`, dropping `src/track_system.o` `.text` again from
  `0x30b04` to `0x30a2c`
- a follow-up SEG1 catalog cleanup centralized repeated cart-catalog upload
  and LOD-name selection logic in `src/track_system.cxx`; this preserved the
  stable build and kept `src/track_system.o` `.text` at `0x30a2c`, so the
  next effective cut likely needs to remove a larger duplicated SEG1 fallback
  block instead of only reshaping helper glue
- a follow-up face-slot dedup pass unified the four fixed/base-rank rebuild and
  resolve loops behind shared family-to-slot helpers in `src/track_system.cxx`,
  dropping `src/track_system.o` `.text` again from `0x30a2c` to `0x306b0`
- a follow-up debug-string compaction pass shortened non-critical
  `track_system` diagnostics in `src/track_system.cxx`, dropping
  `src/track_system.o` `.text` again from `0x306b0` to `0x304a4`
- `cd/data/ISO_PAD_4K.BIN` is now a `6144`-byte inert pad used only to keep
  the stable Saturn image at the required `4134912` bytes while preserving
  the accumulated code-side reductions

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
- a later map-driven pass recovered `0x644` bytes of real code from
  `src/track_system.o`, but the stable-build gate still remains the fixed ISO
  size check, so the image is currently padded back to `4134912`
- `ReuseObservabilityDebugPacket` was narrowed again to decision-only so the
  future Boundary D retry does not carry dead cumulative counters in its debug
  payload
- live SH2 observability overlay labels were compacted again in
  `src/game_loop_track_render_presentation_observability_presenter_ops.hpp`
  to keep reducing always-live debug string weight before reopening Boundary D
- the passive debug siblings in `scheduler/reuse` also dropped dead mode /
  fallback payload fields that were not consumed by any presenter path
- `SchedulerReuseDebugTelemetryPacket` was then narrowed to a `valid`-only
  compile-only marker because downstream presenter summary code did not consume
  any of its other fields
- the presenter observability boundary now stores only a boolean scheduler/reuse
  presence marker instead of carrying the whole scheduler/reuse debug packet
- the facade input boundary above it now carries only `PresenterOverlayDebugPacket`
  instead of the whole presenter observability input packet
- `PresenterInputBundle` also dropped duplicated `overlay` / `observability`
  storage and now derives those summary bits from `observabilityInput` plus
  `overlayDebug`
- presenter bridge packets also dropped dead intermediate fields (`request`,
  `decision`, `facadeDecision`) that were not consumed by the compile-only
  preview chain
- `BuildFrameEndMemoryDebugPresentationBundle()` in `src/game_loop_system.hpp`
  no longer repopulates low-work trace deltas inline; it now reuses the shared
  `CaptureLowWorkTraceDeltaInputs(...)` helper from
  `src/game_loop_memory_trace_packet_assembler.hpp`, which also keeps
  `MaybeLogLowWorkRamTrace()` and frame-end memory presentation on the same
  passive delta-capture path
- `TrackRenderPresentationObservabilityPacket` and
  `TrackRenderSh2PresentationPacket` already use flattened producer-state flags
  instead of caching nested producer-state packets
- `SchedulerReuseObservabilityPacket` now follows the same pattern and stores
  only explicit producer-state flags above the scheduler/reuse boundary
- the host helper path for `TrackRenderProducerHintPacket` now builds straight
  from `TrackRenderTelemetryViewPacket`, removing one local producer-state hop
- `SchedulerReuseObservabilityAssemblyInputs` now also carries only producer
  flags on its local boundary and uses a bool-based overload of
  `BuildSchedulerReuseObservabilityPacket(...)`, keeping the older
  producer-state overload only as a compatibility adapter
- the local host debug path in `src/game_loop_system.hpp` now mirrors the same
  narrowing: `PresentFrameHudAndTelemetry(...)` and `PrintSh2SplitTelemetry(...)`
  pass explicit producer flags instead of a `TrackRenderProducerStatePacket`
- the remaining Track Render presentation assemblers now also prefer direct
  bool/telemetry inputs, and dead host/presenter wrappers around
  `TrackRenderProducerStatePacket` were removed where no live consumer remained
- the last dead compatibility overloads in Track Render presentation/hint
  assemblers were removed once all live callers had already moved to direct
  telemetry/flag inputs
- the same cleanup was then completed in `Scheduler/Reuse`: dead
  `TrackRenderProducerStatePacket` overloads were removed from the observability
  assembler once the assembly boundary had already moved to explicit flags
- with no live consumers left, the last runtime include of the passive
  `TrackRenderProducerStatePacket` pair was removed from `src/game_loop_system.hpp`;
  the headers themselves stay as compatibility shims because the passive/
  observability header smoke validations still compile them as public surface
- the current passive-surface inventory and removal rules are tracked in
  `PASSIVE_COMPATIBILITY_SURFACES.md`
- the presenter facade bridge pair was later confirmed to be shim-only as well:
  no live consumer remains and current references come from passive/observability
  smoke validation includes
- the compile-only presenter preview path was then narrowed again and no longer
  assembles through `PresenterFacadeDecisionBridgePacket`; that bridge pair now
  joins the shim-only compatibility bucket
- the preview packets themselves were then narrowed as well, dropping redundant
  stored decision-input payloads and keeping only the final preview decisions
- the top-level compile-only preview packet was then narrowed too, replacing
  nested preview subpackets with direct stored frame-end/HUD-telemetry
  decision payloads
- with that narrowing complete, the obsolete frame-end/HUD preview subpacket
  contracts and assemblers were removed entirely because they had no remaining
  runtime, compile-only, or smoke-validation consumers
- the top-level compile-only preview packet/assembler pair then also became
  completely orphaned and was removed for the same reason
- the presenter facade decision path also dropped dead facade-interface
  payloads (`PresenterFacadeRequestPacket`, `PresenterFacadePhase`,
  `shouldPresentRenderDebug`, `hasRenderDebug`) that were not consumed outside
  the local decision assembly chain
- the facade bridge layer is no longer consumed by the compile-only preview
  path; its headers remain only as passive compatibility surface
  (`src/game_loop_presenter_facade_bridge_contracts.hpp`,
  `src/game_loop_presenter_facade_bridge_assembler.hpp`)
- the compile-only preview path also stopped building a full
  `PresenterFacadePacket` from `PresenterInputBundle` when it only needed the
  facade decision input, by deriving `PresenterFacadeDecisionInputPacket`
  directly from the input bundle
- `PresenterFacadePacket` and `PresenterFacadeInputPacket` also dropped dead
  transit payloads (`summary`, `render`) because that boundary only still
  consumes driving HUD, periodic HUD, and overlay decisions
- `PresenterInputBundle` also dropped its duplicated `overlayDebug` cache and
  now reads the same payload directly from `observabilityInput.overlayDebug`
  across summary and facade decision assembly
- `PresenterObservabilityInputPacket` also dropped dead raw payload copies
  (`overlay`, `observability`) and now keeps only the consumed derived overlay
  packet plus boolean observability/scheduler-reuse markers
- `PresenterInputBundle` also dropped its dead raw `render` copy and now
  derives `hasRender` from `PresenterRenderDebugPacket.valid`
- `PresenterInputBundle` also dropped its dead cached `summary`, which no
  longer had any readers after the presenter boundary was narrowed
- `PresentationDebugBundle` also dropped the dead stored `realtimeFps` payload
  on the presenter path; builder signatures stay compatible for now, but the
  boundary no longer caches that telemetry
- `PresentationDebugBundle` also dropped the dead stored `frame` snapshot on
  the presenter path; the remaining submitted-face summary now reads from
  `periodicHud`
- after the preview packet removal, the remaining presenter facade/decision
  chain (`PresenterFacade*`, `PresenterFrameEndDecision*`,
  `PresenterHudTelemetryDecision*`) was audited again and is currently
  shim-only as well: no live runtime consumer remains, and the surviving
  includes are self-contained plus the passive/observability smoke-validation
  surface
- that shim-only presenter chain was then split out of the generic
  passive/observability smoke scripts into
  `tools/validate_game_loop_presenter_shim_headers.ps1`, reducing coupling
  between the generic header checks and the removable presenter compatibility
  surface
- with that isolation in place and still no runtime, compile-only, or
  remaining smoke-only consumers, the whole presenter facade/decision shim
  chain and its dedicated smoke script were then removed entirely
- the dead packet-only `TrackRenderPresentationObservability` wrapper was then
  removed as well, leaving only the still-live SH2 presentation path in
  `src/game_loop_track_render_presentation_observability_presenter_ops.hpp`
- the shim-only `TrackRenderProducerState` pair was then split out of the same
  generic smoke scripts into a temporary dedicated validation surface, leaving
  the shared passive/observability checks focused on still-mixed
  runtime-adjacent surfaces
- with that isolation in place and still no runtime consumers, the
  `TrackRenderProducerState` shim pair and its dedicated smoke script were then
  removed entirely
- the orphan `SchedulerReuseObservability` / `SchedulerReuseFlowObservability`
  packet chain and its dead assembly bundle were then removed too; the
  remaining debug-telemetry path now reads directly from lifecycle telemetry
  plus track telemetry
- the host `FinishFrame()` path then dropped its duplicated local fallbacks for
  track telemetry / producer-state extraction and now threads one
  `TrackRenderTelemetryViewPacket` through frame presentation plus HUD
  telemetry
- the dead `SchedulerReuseDebugTelemetry` packet/builder and the unused
  `hasSchedulerReuseDebug` presenter-summary flag were then removed as well
- the overlay presentation printers were split into
  `src/game_loop_overlay_debug_presenter_ops.hpp`, and the segment/query
  snapshot assembly was split into
  `src/game_loop_overlay_runtime_assembler.hpp`, reducing the size of
  `src/game_loop_system.hpp` without changing the frame-end overlay flow
- the low-work overlay path also dropped its redundant local
  `MemoryDebugPresentationBundle` wrapping for overlay-only prints and now
  reads directly from the local `LowWorkOverlayTextBundle` through narrow
  presenter helpers in `src/game_loop_low_work_overlay_presenter_ops.hpp`
- the same low-work overlay path now also centralizes `trackSystem`/memory
  capture into `LowWorkOverlayRuntimePacket` inside
  `src/game_loop_low_work_overlay_capture_ops.hpp`, reducing local variable
  fan-out in `src/game_loop_system.hpp`
- the remaining low-work overlay breakdown/tag-group/allocator state updates
  were then folded into narrow capture/apply helpers in
  `src/game_loop_low_work_overlay_capture_ops.hpp`, shrinking the host-side
  full overlay branch without changing presentation order
- the detailed `HighWorkRamTrace` host path also dropped its local
  H1/H2/H3/H4/L7/L8/L9/L10 assembly by routing through
  `src/game_loop_memory_trace_runtime_debug_ops.hpp`, keeping host gating local
  while moving debug-only capture/presentation out of `src/game_loop_system.hpp`
- the same runtime-debug helper file now also absorbs the local
  `LowWorkRamTrace` text/view assembly and track-draw delta capture, leaving
  `MaybeLogLowWorkRamTrace()` with host gating only
- `TrackRenderPresentationObservabilityPacket` also dropped its dead cached
  `telemetry` payload and now keeps only the consumed producer-state plus SH2
  presentation data
- `TrackRenderProducerHintPacket` also dropped its dead `valid` flag, and
  `TrackRenderSh2PresentationPacket` flattened the consumed producer-state
  booleans instead of carrying the full producer-state packet
- `TrackRenderPresentationObservabilityPacket` also flattened the consumed
  producer-state booleans instead of carrying the full producer-state packet
- the host overlay/debug path in `src/game_loop_system.hpp` then dropped more
  single-use wrapper methods and now calls the canonical overlay presenters
  directly for:
  - segment spatial
  - shadow spatial
  - face/shadow summary
  - ground probe
  - physics query
- the same host cleanup also removed tiny one-use wrappers around submitted
  face counters and simulation-output application in the frame presentation
  path while preserving the stable build envelope
- a follow-up host cleanup removed additional one-use wrappers in the same
  critical file for:
  - track draw trace capture
  - track end trace capture
  - car trace capture
  - track-disabled trace capture
  - driving HUD forwarding
- those paths now call the same canonical capture/presenter logic directly in
  their owning runtime branches while preserving the stable build envelope
- the realtime FPS path in `src/game_loop_system.hpp` also dropped the orphan
  `CaptureIdleRenderTraces()` helper plus its one-use sampling/print wrapper
  stack:
  - `BeginRealtimeFpsSample(...)`
  - `AccumulateRealtimeFpsSample(...)`
  - `BuildRealtimeFpsMetricsSnapshot(...)`
  - `PrintRealtimeFpsMetrics(...)`
  - `ResetRealtimeFpsSampleWindow()`
- `UpdateRealtimeFpsOverlay()` now owns the sample/update/print/reset flow
  directly, keeping the same stable-build envelope while recovering always-live
  host size from the critical file
- the next host-only reduction then inlined the five per-stage work-RAM capture
  wrappers directly into the main frame loop:
  - `CaptureBeginStageTraces()`
  - `CaptureGameplayStageTraces()`
  - `CaptureAutoLapStageTraces()`
  - `CaptureBackgroundStageTraces()`
  - `CaptureHudStageTraces()`
- the same patch also removed dead or one-hop local helpers that no longer had
  any callers in `src/game_loop_system.hpp`:
  - `UpdateLowWorkFreeOverlay()`
  - `PrintWorkRamUsageRealtime()`
  - `MaybeLogHighWorkRamTrace()`
  - `MaybeLogLowWorkRamTrace()`
- frame-end low-work overlay presentation now calls
  `UpdateLowWorkFreeOverlayEnabled<>()` directly, preserving the stable build
  envelope while shrinking the critical host surface further
- outside the host-critical file, the track reuse observability presenter path
  also dropped two forwarding-only helpers in
  `src/game_loop_reuse_observability_debug_bundle_presenter_ops.hpp`:
  - `PresentReuseObservabilityDebugBundle(...)`
  - `TryPresentReuseObservabilityDebugBundle(...)`
- `TryPresentTrackReuseObservabilityDebugBundle(...)` now builds, checks, and
  presents the same debug payload directly while preserving the stable build
  envelope and keeping the public call site unchanged
- the next reuse/scheduler cleanup then decoupled the remaining runtime-owner
  bridge from its active consumers, keeping the compatibility header but
  removing the live dependency on it in:
  - `src/game_loop_reuse_observability_debug_bundle_presenter_ops.hpp`
- `src/game_loop_reuse_runtime_preview_assembler.hpp` was later removed once
  smoke validation no longer depended on it
- the former `src/game_loop_scheduler_reuse_observability_assembly_ops.hpp`
  leaf also assembled reuse inputs from the source-owner path directly instead
  of routing through the runtime-owner bridge before later cleanup removed that
  wrapper
- the active runtime paths continue to depend directly on
  `game_loop_reuse_runtime_owner`
  / source-owner assembly, preserving the stable build envelope and the
  `4134912` ISO size
- the next scheduler-reuse cleanup then removed a remaining one-use seed helper
  from the former `src/game_loop_scheduler_reuse_debug_telemetry_assembler.hpp` and
  returned the debug packet directly from `flow.valid`
- the active presenter-side consumers also no longer route through
  `BuildSchedulerReuseRuntimeDebugTelemetryPacket(...)`; they now build the
  same debug packet directly from the narrow observability/flow assembly in:
  - `src/game_loop_presenter_scheduler_reuse_bridge_assembler.hpp`
  - `src/game_loop_presenter_scheduler_reuse_preview_assembler.hpp`
- the runtime debug bridge header remains available for compatibility/header
  coverage, but the live presenter path no longer depends on it
- the follow-up cleanup confirmed there are now zero `src/` include-sites for
  `game_loop_scheduler_reuse_runtime_debug_bridge_assembler.hpp`
- `src/game_loop_presenter_scheduler_reuse_preview_assembler.hpp` also dropped
  its now-dead include of `game_loop_presenter_scheduler_reuse_bridge_assembler.hpp`
- that leaves the scheduler-reuse runtime debug bridge as a compatibility-only
  shim for header-smoke coverage rather than an active runtime dependency

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
