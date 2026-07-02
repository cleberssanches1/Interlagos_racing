#pragma once

#include "game_loop_track_render_sh2_presentation_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedTrackRenderSh2PresentationPacket(
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2,
    bool useSafeTelemetryFormat,
    uint32_t slaveDispatchCount,
    uint32_t slaveDispatchSkipsTrackBusy,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    TrackRenderSh2PresentationPacket& outPacket)
{
    outPacket.valid = sh2.Valid() || hasProducerState;
    outPacket.useSafeTelemetryFormat = useSafeTelemetryFormat;
    outPacket.slaveDispatchCount = slaveDispatchCount;
    outPacket.slaveDispatchSkipsTrackBusy = slaveDispatchSkipsTrackBusy;
    outPacket.sh2 = sh2;
    outPacket.hasProducerState = hasProducerState;
    outPacket.producerJobInFlight = producerJobInFlight;
    outPacket.producerSafeModeActive = producerSafeModeActive;
}

inline TrackRenderSh2PresentationPacket BuildTrackRenderSh2PresentationPacket(
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2,
    bool useSafeTelemetryFormat,
    uint32_t slaveDispatchCount,
    uint32_t slaveDispatchSkipsTrackBusy,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive)
{
    TrackRenderSh2PresentationPacket packet{};
    SeedTrackRenderSh2PresentationPacket(sh2,
                                         useSafeTelemetryFormat,
                                         slaveDispatchCount,
                                         slaveDispatchSkipsTrackBusy,
                                         hasProducerState,
                                         producerJobInFlight,
                                         producerSafeModeActive,
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
                                                 false,
                                                 false,
                                                 false);
}

} // namespace GameLoopObservabilityDomain
