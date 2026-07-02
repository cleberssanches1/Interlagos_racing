#pragma once

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

inline TrackRenderProducerStatePacket BuildTrackRenderProducerStatePacket(
    const TrackRenderTelemetryViewPacket& telemetryView)
{
    TrackRenderProducerStatePacket packet{};
    SeedTrackRenderProducerStatePacket(telemetryView, packet);
    return packet;
}

} // namespace GameLoopRuntime
