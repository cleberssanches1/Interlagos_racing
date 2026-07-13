#pragma once

#include "game_loop_car_visual_debug_assembler.hpp"
#include "game_loop_render_debug_contracts.hpp"

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

inline void SeedRenderFrameDebugBundle(const TrackRenderFramePacket& track,
                                       const CarVisualFramePacket& car,
                                       RenderFrameDebugBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.track = track;
    outBundle.trackDebug = BuildTrackRenderDebugPacket(track);
    outBundle.car = car;
    outBundle.carDebug = BuildCarVisualDebugPacket(car);
}

inline RenderFrameDebugBundle BuildRenderFrameDebugBundle(const TrackRenderFramePacket& track,
                                                          const CarVisualFramePacket& car)
{
    RenderFrameDebugBundle bundle{};
    SeedRenderFrameDebugBundle(track, car, bundle);
    return bundle;
}

} // namespace GameLoopRuntime
