#pragma once

#include "track_render_contracts.hpp"
#include "game_loop_track_render_packet.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedTrackRenderTelemetryViewPacket(const TrackRenderDomain::TrackRenderTelemetry& telemetry,
                                               TrackRenderTelemetryViewPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.producerJobInFlight = telemetry.producerJobInFlight;
    outPacket.producerSafeModeActive = telemetry.producerSafeModeActive;
    outPacket.masterFrameTicks = telemetry.masterFrameTicks;
    outPacket.slaveProducerTicks = telemetry.slaveProducerTicks;
    outPacket.slaveSortTicks = telemetry.slaveSortTicks;
    outPacket.slavePlanTicks = telemetry.slavePlanTicks;
    outPacket.queryCalls = telemetry.queryCalls;
    outPacket.queryGlobal = telemetry.queryGlobal;
    outPacket.queryScmap = telemetry.queryScmap;
    outPacket.queryCacheHits = telemetry.queryCacheHits;
    outPacket.queryCacheMisses = telemetry.queryCacheMisses;
    outPacket.wallQueryCalls = telemetry.wallQueryCalls;
    outPacket.wallQueryHits = telemetry.wallQueryHits;
}

inline void SeedTrackRenderTelemetryViewPacket(const TrackRenderFramePacket& framePacket,
                                               TrackRenderTelemetryViewPacket& outPacket)
{
    outPacket.valid = framePacket.Valid();
    outPacket.producerJobInFlight = framePacket.telemetry.producerJobInFlight;
    outPacket.producerSafeModeActive = framePacket.telemetry.producerSafeModeActive;
    outPacket.masterFrameTicks = framePacket.telemetry.masterFrameTicks;
    outPacket.slaveProducerTicks = framePacket.telemetry.slaveProducerTicks;
    outPacket.slaveSortTicks = framePacket.telemetry.slaveSortTicks;
    outPacket.slavePlanTicks = framePacket.telemetry.slavePlanTicks;
    outPacket.queryCalls = framePacket.telemetry.queryCalls;
    outPacket.queryGlobal = framePacket.telemetry.queryGlobal;
    outPacket.queryScmap = framePacket.telemetry.queryScmap;
    outPacket.queryCacheHits = framePacket.telemetry.queryCacheHits;
    outPacket.queryCacheMisses = framePacket.telemetry.queryCacheMisses;
    outPacket.wallQueryCalls = framePacket.telemetry.wallQueryCalls;
    outPacket.wallQueryHits = framePacket.telemetry.wallQueryHits;
}

inline TrackRenderTelemetryViewPacket BuildTrackRenderTelemetryViewPacket(
    const TrackRenderFramePacket& framePacket)
{
    TrackRenderTelemetryViewPacket packet{};
    SeedTrackRenderTelemetryViewPacket(framePacket, packet);
    return packet;
}

inline TrackRenderTelemetryViewPacket BuildTrackRenderTelemetryViewPacket(
    const TrackRenderDomain::TrackRenderTelemetry& telemetry)
{
    TrackRenderTelemetryViewPacket packet{};
    SeedTrackRenderTelemetryViewPacket(telemetry, packet);
    return packet;
}

} // namespace GameLoopRuntime
