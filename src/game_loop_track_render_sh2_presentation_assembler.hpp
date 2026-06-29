#pragma once

#include "game_loop_track_render_sh2_presentation_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedTrackRenderSh2PresentationPacket(
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2,
    bool useSafeTelemetryFormat,
    uint32_t slaveDispatchCount,
    uint32_t slaveDispatchSkipsTrackBusy,
    const GameLoopRuntime::TrackRenderProducerStatePacket& producerState,
    TrackRenderSh2PresentationPacket& outPacket)
{
    outPacket.valid = sh2.Valid() || producerState.valid;
    outPacket.useSafeTelemetryFormat = useSafeTelemetryFormat;
    outPacket.slaveDispatchCount = slaveDispatchCount;
    outPacket.slaveDispatchSkipsTrackBusy = slaveDispatchSkipsTrackBusy;
    outPacket.sh2 = sh2;
    outPacket.producerState = producerState;
}

inline TrackRenderSh2PresentationPacket BuildTrackRenderSh2PresentationPacket(
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2,
    bool useSafeTelemetryFormat,
    uint32_t slaveDispatchCount,
    uint32_t slaveDispatchSkipsTrackBusy,
    const GameLoopRuntime::TrackRenderProducerStatePacket& producerState)
{
    TrackRenderSh2PresentationPacket packet{};
    SeedTrackRenderSh2PresentationPacket(sh2,
                                         useSafeTelemetryFormat,
                                         slaveDispatchCount,
                                         slaveDispatchSkipsTrackBusy,
                                         producerState,
                                         packet);
    return packet;
}

inline TrackRenderSh2PresentationPacket BuildTrackRenderSh2PresentationPacket(
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2,
    bool useSafeTelemetryFormat,
    uint32_t slaveDispatchCount,
    uint32_t slaveDispatchSkipsTrackBusy)
{
    return BuildTrackRenderSh2PresentationPacket(sh2,
                                                 useSafeTelemetryFormat,
                                                 slaveDispatchCount,
                                                 slaveDispatchSkipsTrackBusy,
                                                 GameLoopRuntime::TrackRenderProducerStatePacket{});
}

} // namespace GameLoopObservabilityDomain
