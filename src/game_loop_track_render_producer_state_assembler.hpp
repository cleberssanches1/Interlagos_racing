#pragma once

#include "game_loop_track_render_packet.hpp"
#include "game_loop_track_render_producer_state_contracts.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedTrackRenderProducerStatePacket(const TrackRenderTelemetryViewPacket& telemetryView,
                                               TrackRenderProducerStatePacket& outPacket)
{
    outPacket.valid = telemetryView.valid;
    outPacket.producerJobInFlight = telemetryView.producerJobInFlight;
    outPacket.producerSafeModeActive = telemetryView.producerSafeModeActive;
}

inline void SeedTrackRenderProducerStatePacket(const TrackRenderFramePacket& framePacket,
                                               TrackRenderProducerStatePacket& outPacket)
{
    outPacket.valid = framePacket.Valid();
    outPacket.producerJobInFlight = framePacket.telemetry.producerJobInFlight;
    outPacket.producerSafeModeActive = framePacket.telemetry.producerSafeModeActive;
}

inline TrackRenderProducerStatePacket BuildTrackRenderProducerStatePacket(
    const TrackRenderTelemetryViewPacket& telemetryView)
{
    TrackRenderProducerStatePacket packet{};
    SeedTrackRenderProducerStatePacket(telemetryView, packet);
    return packet;
}

inline TrackRenderProducerStatePacket BuildTrackRenderProducerStatePacket(
    const TrackRenderFramePacket& framePacket)
{
    TrackRenderProducerStatePacket packet{};
    SeedTrackRenderProducerStatePacket(framePacket, packet);
    return packet;
}

} // namespace GameLoopRuntime
