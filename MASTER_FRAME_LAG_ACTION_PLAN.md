# Master Frame Lag Action Plan

## Objective

Keep the existing 20-segment resident track window and stable texture recycling,
while reducing the number of segments submitted to SGL/VDP1 and exposing the
Master-side time that was previously hidden behind the aggregate frame time.

## Implemented stages

1. Master phase telemetry
   - measure gameplay, background/late simulation consume, HUD, car/shadow,
     `SRL::Core::Synchronize`, and post-sync overlays;
   - report peak pose age and the peak number of draw-culled segments;
   - retain the existing texture, Slave physics, and track timing row.
2. Input-to-pose latency
   - consume a completed Slave simulation packet a second time immediately
     before camera resolution;
   - never wait for the Slave in this late consume.
3. Car render hot path
   - keep flat-light and sort attributes configured at model bootstrap;
   - remove the per-frame full-face attribute walk.
4. Conservative track submission
   - preserve all resident segments and collision data;
   - always submit the supporting segment and two route neighbours on each side;
   - reject only a segment whose complete XZ AABB is behind an expanded camera
     plane;
   - retain a previously visible segment for three frames to prevent popping.
5. Long-stall phase telemetry
   - pair the 16-bit SH-2 FRT with the 32-bit application VBlank counter;
   - measure camera, track stream, track draw and `TrackSystem::EndFrame`;
   - classify stalls of roughly 100 ms or more by Master phase;
   - retain the worst phase, VBlank duration and frame id after rendering resumes;
   - keep all state fixed-size and print only on the existing 60-frame cadence.

## HUD interpretation

The new row is:

`M g.. b.. h.. c.. y.. o.. a.. z..`

- `g`: gameplay peak in milliseconds;
- `b`: background plus late non-blocking simulation consume;
- `h`: HUD/update/presentation;
- `c`: car and shadow submission;
- `y`: `Synchronize` duration;
- `o`: post-sync overlays;
- `a`: maximum rendered pose age in frames;
- `z`: maximum number of resident segments omitted from drawing.

The existing `m` remains the complete `TrackSystem::RenderFrame` peak and `s`
remains the Slave physics peak.

The additional rows are:

`P ca.. ts.. td.. te.. un..`

- `ca`: camera resolution peak;
- `ts`: track streaming/planning peak;
- `td`: track SGL submission peak;
- `te`: `TrackSystem::EndFrame` maintenance peak;
- `un`: frame time not covered by the named Master phases (`99` when the FRT
  cannot be trusted because the frame crossed the long-stall threshold).

`L XX vb.. ev.. fr..`

- `XX`: phase code (`TR`, `TE`, `SY`, `CA`, `HD`, `FR`, and others);
- `vb`: duration of the worst stall in VBlanks;
- `ev`: number of long-stall events observed;
- `fr`: frame id of the worst event.

## Runtime acceptance test

1. Drive at least three complete laps in the normal direction.
2. Repeat with reversals and a 180-degree turn.
3. Check sharp curves around segments 147-159 for missing geometry.
4. Record the `FPS`, `V`, and `M` rows at the same circuit positions.
5. Target `d30 <= 5%`, no missing segments, pose age `a <= 1`, and stable
   texture counters (`f/n` remain zero after warm-up and `k` stays stable).
6. If `y` remains the dominant peak, use `z` and submitted-face telemetry to
   tune only the draw visibility margin or distant visual budget; do not reduce
   collision geometry or the resident window.

## Rollback seams

- Disable only the conservative rejection in
  `TrackSystem::RenderVisibleSegmentOrderStabilized` to restore submission of
  all resident entries.
- Remove only the second `ConsumeCompletedJobs` call to restore the previous
  one-frame async presentation policy.
- Final SGL/VDP1 submission and PCM/SGL audio ownership remain on Master SH-2.
