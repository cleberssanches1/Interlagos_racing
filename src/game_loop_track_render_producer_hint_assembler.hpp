#pragma once

#include "game_loop_track_render_producer_hint_contracts.hpp"
#include "game_loop_track_render_producer_state_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedTrackRenderProducerHintPacket(const TrackRenderProducerStatePacket& producerState,
                                              TrackRenderProducerHintPacket& outPacket)
{
    outPacket.valid = producerState.valid;
    outPacket.producerJobInFlight = producerState.producerJobInFlight;
}

inline TrackRenderProducerHintPacket BuildTrackRenderProducerHintPacket(
    const TrackRenderProducerStatePacket& producerState)
{
    TrackRenderProducerHintPacket packet{};
    SeedTrackRenderProducerHintPacket(producerState, packet);
    return packet;
}

} // namespace GameLoopRuntime
