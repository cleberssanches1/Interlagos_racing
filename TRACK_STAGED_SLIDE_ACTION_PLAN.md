# Track Staged Slide Action Plan

## Objective

Remove the synchronous track-belt spike observed on long frames while preserving
the fixed resident window, bidirectional travel, texture-slot recycling, and the
current Master/Slave ownership boundaries.

## Verified cause

The long-frame telemetry correlates stalls with `x1`.  `TrackSystem::BeginFrame`
could build the incoming renderer and execute as many as three slides before the
internal track timer started.  A metadata-only prefetch was also treated as
complete, allowing the slide path to fall back to synchronous geometry build.
In reverse travel, the single prefetch scratch was overwritten with an extra
future segment, evicting the segment needed by the immediate slide.

## Implementation

1. Add a pure staged-slide decision policy with host tests.
2. Treat a prefetch as slide-ready only when its target, family metadata, and
   renderer all match the incoming segment.
3. In `BeginFrame`, perform one bounded action:
   - prepare the missing prefetch; or
   - commit one already-prepared slide.
4. Do not prefetch another segment in a frame that already committed a slide.
5. Remove the reverse extra-prefetch overwrite because only one scratch renderer
   exists.
6. Keep the old behavior behind one local rollback constant.
7. Extend long-frame context with the prefetch-build attempt count so a new
   recording distinguishes preparation (`p1 x0`) from commit (`p0 x1`).

## Validation

1. Run the track streaming policy host tests.
2. Run passive and observability header validation.
3. Compile the SH-2 target without rebuilding assets.
4. In emulator, drive multiple laps in both directions and compare long events:
   - no missing resident segments;
   - at most `x1` in one frame;
   - prefetch and slide do not appear together (`p1 x1` must not occur);
   - long-event count and `vb` peaks decrease.

## Rollback

Set `kEnableStagedSlidePipeline` to `false` in `track_system.cxx`.  This restores
the previous catch-up and immediate post-slide prefetch behavior without changing
the generated track or texture data.
