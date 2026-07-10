# Track Render Passive Flow Plan

## Objective

Prepare the extraction of track-side scheduling/consumption out of
`src/game_loop_system.hpp` without changing the current lockstep runtime.

Consolidated passive-contract index:

- `PASSIVE_CONTRACTS_INVENTORY.md`

## Current passive building blocks

- `src/track_render_contracts.hpp`
- `src/track_render_state_assembler.hpp`
- `src/track_render_telemetry_assembler.hpp`
- `src/track_render_transition_ops.hpp`
- `src/track_render_scheduler.hpp`
- `src/game_loop_track_render_packet.hpp`
- `src/game_loop_track_render_packet_assembler.hpp`
- `src/game_loop_track_render_telemetry_view_contracts.hpp`
- `src/game_loop_track_render_telemetry_view_assembler.hpp`

These files already describe a passive track path for:

- frame-context assembly
- render-packet assembly
- telemetry assembly
- frame-local aggregation of the track data consumed by the Master

## Current runtime touch points

Track-side packet/telemetry logic currently appears in `src/game_loop_system.hpp`
through small direct calls such as:

1. `IsTrackProducerJobInFlightHint(...)`
2. overlay query population via `BuildTrackRenderTelemetry(...)`
3. SH2 split telemetry assembly via `BuildTrackRenderTelemetry(...)`

This means the runtime already consumes passive track telemetry, but without a
single frame-local packet that groups:

- the scheduling context
- the render packet
- the telemetry snapshot

## Passive packet model

`src/game_loop_track_render_packet.hpp` aggregates:

- `FrameContext`
- `RenderPacket`
- `Telemetry`

This stays:

- frame-local
- non-owning
- runtime-neutral until explicit integration is needed

## Runtime-to-passive substitution map

### Frame context

Passive coverage already available:

1. `Game::TrackRenderScheduler::BuildFrameContext(...)`
2. `TrackRenderDomain::SeedTrackFrameContext(...)`

Main source data that will eventually feed it:

- `frameCounter_`
- `latestActiveSegmentId_`
- `context_.RenderTrack()`
- `context_.trackSegOffset`
- `context_.lightDirection`
- camera location / look target
- `context_.carWorldPosition`

### Render packet

Passive coverage already available:

1. `Game::TrackRenderScheduler::BuildRenderPacket(...)`
2. `TrackRenderDomain::SeedTrackRenderPacket(...)`
3. `TrackRenderDomain::SeedTrackRenderPacketProducerFlags(...)`

Current runtime-relevant outputs:

- `renderPacket.valid`
- `renderPacket.observedCarSegmentId`
- `renderPacket.submittedTrackFaces`
- `renderPacket.usedSlaveProducer`
- `renderPacket.usedSlaveSort`

### Telemetry

Passive coverage already available:

1. `Game::TrackRenderScheduler::BuildTelemetry(...)`
2. `TrackRenderDomain::BuildTrackRenderTelemetry(...)`
3. `TrackRenderDomain::SeedTrackRenderTelemetry(...)`

Current runtime-relevant outputs:

- producer in-flight hint
- query counters for overlays
- SH2 split counters
- producer safe-mode state

## Safe integration order

### Step 1 - keep runtime consumption unchanged

Do not change:

- lockstep producer/sort behavior
- render submission order
- safe-mode fallback
- any `N-1` reuse decision

### Step 2 - local packet assembly only

When runtime integration becomes safe, assemble locally:

1. `FrameContext`
2. `RenderPacket`
3. `Telemetry`
4. `TrackRenderFramePacket`

Consume immediately in the same function.

No new persistent members.
No ownership transfer.
No async policy changes.

### Step 3 - replace repeated telemetry fetches first

The best first runtime candidate is not producer scheduling itself.

It is:

- reuse one local track packet across:
  - `IsTrackProducerJobInFlightHint(...)`
  - overlay query/producer diagnostics
  - SH2 telemetry presentation

This is the lowest-risk place to reduce repeated ad hoc track telemetry fetches
without touching render ownership.

### Step 4 - only later revisit reuse/latency decisions

Only after repeated stable emulator runs:

- make previous-frame reuse explicit
- isolate the decision packet for `N-1` track consumption
- keep a synchronous fallback path

## Guard rails

- do not mix this extraction with car-audio or drivetrain changes
- do not reintroduce `N-1` behavior in the same patch that introduces the packet
- do not alter `TrackSystem` producer/sort ownership yet
- validate every step with `tools/validate_saturn_stable_build.ps1`
- keep ISO exactly `4134912`

## Immediate next safe step

The next safe step is:

1. keep the current runtime unchanged
2. use this packet only as passive groundwork
3. later try a tiny runtime cut that shares one telemetry snapshot across the
   track overlay and SH2 presentation paths

## Validation coverage

Current compile-only SH2 validation now covers:

- `src/game_loop_track_render_packet.hpp`
- `src/game_loop_track_render_packet_assembler.hpp`

through:

- `tools/validate_game_loop_passive_headers.ps1`

## New passive render aggregation

The track render slice now also participates in one higher-level passive render bundle:

- `src/game_loop_render_debug_contracts.hpp`
- `src/game_loop_render_debug_assembler.hpp`

Current effect:

- `TrackRenderFramePacket` can now be grouped off-path with `CarVisualFramePacket`
- this prepares a future render/presenter facade without touching producer/sort ownership
- runtime scheduling and `N-1` policy remain untouched

An additional passive derived-debug slice is now also available for the track path:

- `src/game_loop_track_render_debug_contracts.hpp`
- `src/game_loop_track_render_debug_assembler.hpp`

Current effect:

- `TrackRenderFramePacket` can now be reduced off-path into a smaller
  `TrackRenderDebugPacket`
- future presenter/debug formatting can consume track scheduling/render state
  without walking the full frame-context/render/telemetry packet structure
- runtime scheduler ownership remains untouched

Another narrow passive telemetry slice is now also available for the track path:

- `src/game_loop_track_render_telemetry_view_contracts.hpp`
- `src/game_loop_track_render_telemetry_view_assembler.hpp`

Current effect:

- `TrackRenderFramePacket` can now be reduced off-path into a smaller
  `TrackRenderTelemetryViewPacket`
- narrow live consumers can share one reduced telemetry surface without pulling
  the full frame packet into the presentation path

One additional compile-only presentation/observability aggregate now exists
above the narrow telemetry/producer-state boundary:

- `src/game_loop_track_render_presentation_observability_contracts.hpp`
- `src/game_loop_track_render_presentation_observability_assembler.hpp`
- `src/game_loop_track_render_presentation_observability_presenter_ops.hpp`
- `src/game_loop_track_render_sh2_presentation_contracts.hpp`
- `src/game_loop_track_render_sh2_presentation_assembler.hpp`

Current effect:

- `TrackRenderTelemetryViewPacket`
- `TrackRenderProducerStatePacket`
- `Sh2SplitTelemetrySnapshot`

can now be grouped off-path into one local passive packet for future
presentation/debug retries without touching producer/sort ownership
- the producer-state print formatting can also be retried through one external
  presenter helper instead of reintroducing inline formatting in the host

Another compile-only packet now exists one level lower for the exact
`PrintSh2SplitTelemetry(...)` boundary:

- `TrackRenderSh2PresentationPacket`

Current effect:

- the SH2 snapshot
- the optional producer-state view
- the safe/fallback presentation mode
- the two host-local dispatch counters

can now be grouped for a future remove-first live retry without broadening the
boundary beyond presentation/debug
  `TrackRenderTelemetryViewPacket`
- the exact telemetry fields currently repeated across:
  - producer in-flight hint
  - overlay query diagnostics
  - SH2 query/busy presentation
  are now available in one narrow packet
- a future retry can target shared telemetry consumption first without pulling
  the broader frame-context/render packet into the live call site

The exact future retry order for this narrow packet is now documented in:

- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

## Current narrow boundary chain

The live track boundary is now intentionally layered:

1. `TrackRenderTelemetryViewPacket`
2. `TrackRenderProducerStatePacket`
3. `TrackRenderProducerHintPacket`

Current purpose of each layer:

- `TrackRenderTelemetryViewPacket`
  - shared telemetry slice for overlay and SH2 presentation
  - keeps repeated counters in one packet
- `TrackRenderProducerStatePacket`
  - isolates producer-only state from the broader telemetry slice
  - preserves `producerSafeModeActive` for future non-scheduling decisions
- `TrackRenderProducerHintPacket`
  - narrows the live hint path to only `producerJobInFlight`
  - keeps the actual live consumer surface minimal

This means the runtime no longer needs to read the broader track telemetry
shape at the final hint call site.

## Next passive reuse groundwork

The next low-risk passive target is the track reuse decision boundary, not the
scheduler itself.

That boundary should remain compile-only first and expose only the final
decision shape needed by a future live caller, for example:

- consume committed packet or not
- kick producer or not
- lockstep wait / synchronous fallback flags
- selected consume/dispatch slots

This keeps `TrackReuseDecisionPacket` available upstream while preparing a
smaller presentation/runtime-facing packet for later use.

That next reuse groundwork now has two passive layers:

- `TrackReuseDecisionViewPacket`
- `TrackReuseTelemetryViewPacket`

One higher track-only aggregate now also exists above them:

- `TrackReuseObservabilityPacket`

One compile-only preview now also exists above that aggregate:

- `TrackReusePreviewPacket`

One compile-only presenter/debug helper now also exists above that preview:

- `PresentTrackReusePreviewPacket(...)`

One compile-only runtime bridge now also exists aligned to the real host seam:

- `BuildTrackReuseRuntimeObservabilityPacket(...)`
- `BuildTrackReuseRuntimePreviewPacket(...)`

One compile-only presenter helper now also exists directly above that seam:

- `PresentTrackReuseRuntimePreviewPacket(...)`

One narrow live seam is now also accepted at that same host anchor:

- `src/game_loop_system.hpp` captures one local `TrackReuseRuntimeState`
- `RenderTrackFrame(...)` now records the narrow request-side and committed
  history data for track reuse
- `TryBuildReuseObservabilityDebugBundle(...)` now builds one real track-only
  `FrameReuseRuntimeOwnerPacket`
- simulation-side reuse remains neutral there

Current split:

- `TrackReuseDecisionViewPacket`
  - final decision flags/slots
- `TrackReuseTelemetryViewPacket`
  - accumulated track reuse counters only
- `TrackReuseObservabilityPacket`
  - narrow track-only aggregate for future live callers before they need the
    broader cross-domain `ReuseObservabilityPacket`
- `TrackReusePreviewPacket`
  - compile-only inspection point for
    `TrackReuseDecisionViewPacket + TrackReuseTelemetryViewPacket ->
    TrackReuseObservabilityPacket`
- `PresentTrackReusePreviewPacket(...)`
  - compile-only textual consumer for the same chain
- `BuildTrackReuseRuntimeObservabilityPacket(...)`
  - compile-only bridge from `FrameReuseRuntimeOwnerPacket` into the narrow
    track-only aggregate
- `BuildTrackReuseRuntimePreviewPacket(...)`
  - compile-only bridge from the same seam into the preview guard rail
- `PresentTrackReuseRuntimePreviewPacket(...)`
  - compile-only presenter/debug helper immediately above that seam
- accepted live seam at the same anchor
  - real track-only runtime-owner capture only
  - no simulation-side live reuse yet

This keeps future live retry options narrow on both the decision side and the
telemetry side without pulling the full `FrameReuseTelemetry` object into a
critical call site.

## Reuse symmetry status

The passive reuse groundwork is now structurally symmetric:

- `TrackReuseDecisionViewPacket`
- `TrackReuseTelemetryViewPacket`
- `SimulationReuseDecisionViewPacket`
- `SimulationReuseTelemetryViewPacket`

Current intent:

- keep track and simulation reuse boundaries parallel
- reduce future live substitutions to equivalent narrow packets
- avoid mixing one-sided reuse refactors with runtime scheduling changes

## Reuse observability aggregation

The reuse family can now also be grouped passively into one higher-level
observability packet:

- `ReuseObservabilityPacket`

It groups only the narrow reuse views:

- `SimulationReuseDecisionViewPacket`
- `SimulationReuseTelemetryViewPacket`
- `TrackReuseObservabilityPacket`

This broader cross-domain packet is intentionally still compile-only groundwork
above the accepted track-only low live seam.

Current benefit:

- future presenter/debug consumers can accept one reuse bundle instead of
  walking four separate packets
- no runtime loop ownership or scheduling behavior changes

## Scheduler / reuse observability aggregation

One higher-level passive bundle can now sit above the reuse family:

- `SimulationSchedulerTelemetryViewPacket`
- `SchedulerReuseObservabilityPacket`

Current intent:

- join simulation-scheduler telemetry with track producer state and the full
  reuse observability bundle
- prepare a future debug/presenter consumer that reasons about scheduling
  pressure and reuse behavior together
- keep this aggregation compile-only until an explicit remove-first runtime cut
  exists

The dedicated hierarchy/order for this family is now documented in:

- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`

The first accepted live retry for the track-only branch is now documented in:

- `TRACK_REUSE_OBSERVABILITY_FIRST_LIVE_RETRY_PLAN.md`
- `TRACK_REUSE_OBSERVABILITY_FIRST_LIVE_RETRY_PATCH_PLAN.md`

The next safe move above that seam is not another broad aggregate jump.

It is:

- complete only the missing symmetric simulation-side branch in the same local
  reuse seam
