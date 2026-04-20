# Segment Creation Alternatives and Action Plans

## Context from current logs

Observed in your latest capture:
- `SWLWR free` drops from about `711320` to about `700820` while racing.
- `LWP rst/prp/cmt/mrg/hnd/drw/efr` stays near `0` most frames.
- `LWC2 f/t/m` stays close to `f:13964 t:5764 m:14488`.
- FPS degrades as free memory drops.

Interpretation:
- The drop is likely not dominated by `EndFrame` cleanup anymore.
- The drop appears outside current stage probes or outside tracked retained-capacity buckets.
- Most probable root cause is allocation churn/fragmentation in the segment build path (or another unprobed path), not a single large retained vector growing visibly.

## Current segment creation path (today)

Current flow (summary):
1. `BuildSegmentIntoPrefetch(...)` decides and prepares next segment.
2. `BuildSegmentIntoRenderer(...)` builds geometry from `RDR/SDR` blob.
3. `BuildRendererFromRdr(...)` / `BuildRendererFromSdr(...)` decode vertices/faces/attrs with repeated push operations.
4. `TrackRenderer::InitializeFromComponentDataRecycled(...)` moves data into renderer-owned vectors.
5. Slide commits by swapping renderer/state into active window.

Main weak points:
- Repeated decode + vector growth patterns in build path.
- Repeated ownership transitions between scratch vectors and renderer vectors.
- Probe coverage does not isolate per-function allocation deltas inside all decode/build helpers.

## Alternative implementations for segment creation

### Option A (recommended): Fixed geometry slot arena (zero-alloc runtime path)

Idea:
- Replace dynamic per-build vectors with fixed preallocated slot memory sized by pack maxima:
  - `maxVertexCount`, `maxFaceCount`, `maxFamilyCount`.
- Each active segment slot owns fixed buffers:
  - verts, faces, attrs, familyIds, rankOffsets, currentFaceSlots.
- Segment creation writes directly into slot buffers and only updates `count` fields.
- Slide/prefetch only swaps slot indices/handles, no runtime allocation.

Expected impact:
- Strong reduction of allocator churn and fragmentation.
- Deterministic memory profile after warm-up.
- Better frame-time stability at segment boundaries.

Tradeoffs:
- Higher baseline reserved memory.
- Requires renderer API to read external slot buffers (or internal fixed buffers).

Complexity: Medium/High
Risk: Medium

### Option B: Bump arena for build/transient decode (incremental migration)

Idea:
- Keep current logic, but route all build-time temporary allocations to a dedicated linear arena.
- Arena reset once per frame or once per slide commit.
- No free calls in hot path; no TLSF fragmentation for transients.

Expected impact:
- Fast to implement.
- Removes most transient allocator churn.
- Smaller code churn than Option A.

Tradeoffs:
- Renderer may still own dynamic buffers unless combined with capacity floors.
- Not as deterministic as full fixed slots.

Complexity: Medium
Risk: Low/Medium

### Option C: Prebuilt segment package cache + pointer swap

Idea:
- Build or load prepacked segment geometry payloads once (startup or background).
- Runtime only selects package by segment id and binds pointers.
- Slide commits become pointer swaps + slot remap.

Expected impact:
- Minimal runtime build cost.
- Stable frame times if cache fits memory budget.

Tradeoffs:
- Higher memory pressure at startup.
- Requires package format versioning and cache eviction strategy if full track does not fit.

Complexity: High
Risk: Medium/High

### Option D: Two-tier representation (far metadata, near full geometry)

Idea:
- Keep full geometry only for near window.
- Mid/far segments use reduced representation (family + coarse geometry/impostor).
- Hydrate full geometry only when entering near ring.

Expected impact:
- Lower peak memory.
- Better scalability for larger tracks.

Tradeoffs:
- Most complex logic and highest gameplay/render risk.
- Requires visual transition handling.

Complexity: Very High
Risk: High

## Decision matrix

| Option | Memory stability | FPS stability | Engineering cost | Risk |
|---|---|---|---|---|
| A Fixed slot arena | Very high | Very high | High | Medium |
| B Bump arena | High | Medium/High | Medium | Low/Medium |
| C Prebuilt package cache | High | High | High | Medium/High |
| D Two-tier representation | High | Medium/High | Very high | High |

Recommendation:
1. Start with Option B as a short cycle to confirm the root cause quickly.
2. If confirmed, move to Option A as the long-term architecture.

## Action plan 1 (short cycle, 3-5 days): Option B

### Phase 0 - Probe expansion (mandatory before refactor)
- Add LWR probes around:
  - `BuildRendererFromRdr`
  - `BuildRendererFromSdr`
  - `BuildSegmentIntoRenderer`
  - `TrackRenderer::InitializeFromComponentDataRecycled`
- Add counters:
  - build calls/frame
  - decode bytes/frame
  - compact/trim calls/frame
- Add one overlay line with these counters.

Exit criteria:
- Leak/churn signature clearly associated with build path.

### Phase 1 - Arena integration
- Implement `TrackBuildArena` in LWR with fixed blocks.
- Replace temporary decode vectors with arena-backed buffers.
- Reset arena once at end of segment build cycle.

Exit criteria:
- `SWLWR free` stabilizes after warm-up during a 20-30 min soak.

### Phase 2 - Validation
- Test scenarios:
  - steady lap at high speed
  - frequent slide transitions
  - long soak (>=30 min)
- Record: FPS min/avg and free-memory slope.

Success target:
- Near-zero free-memory slope after warm-up (no monotonic downtrend).

## Action plan 2 (long cycle, 1-2 weeks): Option A

### Phase 0 - Data model
- Create `SegmentGeometrySlot` with fixed capacities from runtime pack header.
- Preallocate `windowSize + scratch + prefetch` slots at init.

### Phase 1 - Builder rewrite
- New builders write directly into slot arrays:
  - `BuildSegmentIntoSlotFromRdr`
  - `BuildSegmentIntoSlotFromSdr`
- Remove per-build vector ownership transfers.

### Phase 2 - Renderer binding mode
- Add non-owning bind API in `TrackRenderer` for slot buffers.
- Keep old API as fallback behind feature flag.

### Phase 3 - Slide/prefetch integration
- Convert `ExecuteDeterministicStabilizedSlide` and prefetch path to slot index swaps.
- Keep family/texture state mapping unchanged initially.

### Phase 4 - Hardening
- Add invariants:
  - no allocation after init in hot path
  - slot counts never exceed capacity
- Soak and stress tests.

Success target:
- Zero runtime allocations in segment creation path after initialization.
- Stable FPS and stable `SWLWR free` after warm-up.

## Suggested implementation order in this repo

1. Add probes first in `src/track_system.cxx` and `src/track_renderer.hpp`.
2. Implement Option B arena path behind compile flag (`TRACK_SEG_BUILD_ARENA`).
3. Run soak, compare slope of free-memory trend.
4. If trend solved, plan migration to Option A fixed slots.
5. Keep fallback path for fast rollback during tests.

## Files likely affected

- `src/track_system.hpp`
- `src/track_system.cxx`
- `src/track_renderer.hpp`
- `src/game_loop_system.hpp` (extra telemetry line)
- `tests/track_segment_creation_lwr_tests.cpp` (new stress cases)

## Test protocol for each attempt

Use same route and same camera mode across attempts.
Collect at least:
- 30 minutes runtime log
- start free LWR and end free LWR
- FPS min/avg
- count of segment builds and slides

Acceptance gate:
- free-memory slope approx 0 after warm-up
- no progressive FPS degradation
- no new texture/slot regressions
