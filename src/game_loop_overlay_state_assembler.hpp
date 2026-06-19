#pragma once

#include "game_loop_overlay_contracts.hpp"

namespace GameLoopOverlayDomain
{

inline void SeedSegmentOverlayPacket(const GameLoopRuntime::SegmentOverlaySnapshot& snapshot,
                                     bool windowValid,
                                     bool centerValid,
                                     SegmentOverlayPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.windowValid = windowValid;
    outPacket.centerValid = centerValid;
    outPacket.snapshot = snapshot;
}

inline void SeedWindowOverlayPacket(const GameLoopRuntime::SegmentOverlaySnapshot& snapshot,
                                    bool windowValid,
                                    WindowOverlayPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.windowValid = windowValid;
    outPacket.windowStartId = snapshot.windowStartId;
    outPacket.windowDir = snapshot.windowDir;
    outPacket.windowCount = snapshot.windowCount;
    outPacket.sequence = snapshot.seq;
}

inline void SeedNearestSegmentPacket(const GameLoopRuntime::SegmentOverlaySnapshot& snapshot,
                                     NearestSegmentPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.activeSegmentId = snapshot.carSegmentId;
    outPacket.nearestSegmentId = snapshot.nearestSegmentId;
}

inline void SeedOverlayDiagnosticsPacket(const GameLoopRuntime::OverlayDiagnosticsSnapshot& snapshot,
                                         const Game::CarSystem::RuntimeDebugSnapshot& carDebug,
                                         const Game::CarSystem::GameplayInputSnapshot& input,
                                         uint32_t submittedTrackFaces,
                                         uint32_t submittedCarFaces,
                                         OverlayDiagnosticsPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.submittedTrackFaces = submittedTrackFaces;
    outPacket.submittedCarFaces = submittedCarFaces;
    outPacket.carDebug = carDebug;
    outPacket.input = input;
    outPacket.snapshot = snapshot;
}

inline void AttachTrackTelemetry(const TrackRenderDomain::TrackRenderTelemetry& telemetry,
                                 OverlayDiagnosticsPacket& ioPacket)
{
    ioPacket.hasTrackTelemetry = true;
    ioPacket.trackTelemetry = telemetry;
}

inline void SeedOverlayEventPacket(const GameLoopRuntime::OverlayEventState& state,
                                   const GameLoopRuntime::SegmentOverlaySnapshot& snapshot,
                                   OverlayEventPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.carSegmentChanged = state.CarSegmentChanged(snapshot);
    outPacket.windowStartChanged = state.WindowStartChanged(snapshot);
    outPacket.prevCarSegmentId = state.prevCarSegmentId;
    outPacket.nextCarSegmentId = snapshot.carSegmentId;
    outPacket.prevWindowStartId = state.prevWindowStartId;
    outPacket.nextWindowStartId = snapshot.windowStartId;
}

} // namespace GameLoopOverlayDomain
