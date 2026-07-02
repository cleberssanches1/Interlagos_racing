#pragma once

#include "game_loop_track_render_producer_hint_contracts.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedTrackRenderProducerHintPacket(const TrackRenderTelemetryViewPacket& telemetryView,
                                              TrackRenderProducerHintPacket& outPacket)
{
    outPacket.producerJobInFlight = telemetryView.producerJobInFlight;
}

inline TrackRenderProducerHintPacket BuildTrackRenderProducerHintPacket(
    const TrackRenderTelemetryViewPacket& telemetryView)
{
    TrackRenderProducerHintPacket packet{};
    SeedTrackRenderProducerHintPacket(telemetryView, packet);
    return packet;
}

} // namespace GameLoopRuntime
