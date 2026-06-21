#pragma once

#include "game_loop_track_render_debug_contracts.hpp"
#include "game_loop_track_render_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedTrackRenderDebugPacket(const TrackRenderFramePacket& framePacket,
                                       TrackRenderDebugPacket& outPacket)
{
    outPacket.valid = framePacket.Valid();
    outPacket.renderEnabled = framePacket.frameContext.renderEnabled;
    outPacket.usedSlaveProducer = framePacket.renderPacket.usedSlaveProducer;
    outPacket.usedSlaveSort = framePacket.renderPacket.usedSlaveSort;
    outPacket.producerSafeModeActive = framePacket.telemetry.producerSafeModeActive;
    outPacket.producerJobInFlight = framePacket.telemetry.producerJobInFlight;
    outPacket.shouldReducePressure = framePacket.budgetPolicy.shouldReducePressure;
    outPacket.shouldAvoidOptionalAllocations =
        framePacket.budgetPolicy.shouldAvoidOptionalAllocations;
    outPacket.frameId = framePacket.frameContext.frameId;
    outPacket.observedCarSegmentId = framePacket.renderPacket.observedCarSegmentId;
    outPacket.submittedTrackFaces = framePacket.renderPacket.submittedTrackFaces;
    outPacket.queryCalls = framePacket.telemetry.queryCalls;
    outPacket.wallQueryCalls = framePacket.telemetry.wallQueryCalls;
}

inline TrackRenderDebugPacket BuildTrackRenderDebugPacket(
    const TrackRenderFramePacket& framePacket)
{
    TrackRenderDebugPacket packet{};
    SeedTrackRenderDebugPacket(framePacket, packet);
    return packet;
}

} // namespace GameLoopRuntime
