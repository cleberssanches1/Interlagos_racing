#pragma once

#include <srl.hpp>

#include "game_loop_track_render_presentation_observability_contracts.hpp"
#include "game_loop_track_render_sh2_presentation_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void PresentTrackRenderSh2BusyLine(
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& snapshot)
{
    if (!snapshot.Valid())
    {
        return;
    }

    SRL::Debug::Print(1, 24, "S2 b:%u/%u mb:%u sw:%u",
                      static_cast<unsigned>(snapshot.masterBusyPct),
                      static_cast<unsigned>(snapshot.slaveWorkPct),
                      static_cast<unsigned>(snapshot.masterBusyTicks),
                      static_cast<unsigned>(snapshot.slaveWorkTicks));
}

inline void PresentTrackRenderSh2SimSafeLine(
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& snapshot)
{
    if (!snapshot.Valid())
    {
        return;
    }

    SRL::Debug::Print(1, 25, "S2 s:%u w:%u q:%u g:%u m:%u",
                      static_cast<unsigned>(snapshot.simSlaveTicks),
                      static_cast<unsigned>(snapshot.masterWaitTicks),
                      static_cast<unsigned>(snapshot.queryCalls),
                      static_cast<unsigned>(snapshot.queryGlobal),
                      static_cast<unsigned>(snapshot.queryScmap));
}

inline void PresentTrackRenderSh2SimFallbackLine(
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& snapshot,
    uint32_t slaveDispatchCount,
    uint32_t slaveDispatchSkipsTrackBusy)
{
    if (!snapshot.Valid())
    {
        return;
    }

    SRL::Debug::Print(1, 25, "S2 sl:%u w:%u %u%% d:%u tb:%u",
                      static_cast<unsigned>(snapshot.simSlaveTicks),
                      static_cast<unsigned>(snapshot.masterWaitTicks),
                      static_cast<unsigned>(snapshot.masterWaitPctOfSim),
                      static_cast<unsigned>(slaveDispatchCount),
                      static_cast<unsigned>(slaveDispatchSkipsTrackBusy));
}

inline void PresentTrackRenderProducerStatePacket(
    const GameLoopRuntime::TrackRenderProducerStatePacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    SRL::Debug::Print(1, 26, "S2 p:%u sm:%u   ",
                      static_cast<unsigned>(packet.producerJobInFlight ? 1u : 0u),
                      static_cast<unsigned>(packet.producerSafeModeActive ? 1u : 0u));
}

inline void PresentTrackRenderPresentationObservabilityPacket(
    const TrackRenderPresentationObservabilityPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    PresentTrackRenderSh2BusyLine(packet.sh2);
    PresentTrackRenderProducerStatePacket(packet.producerState);
}

inline void PresentTrackRenderSh2PresentationPacket(
    const TrackRenderSh2PresentationPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    PresentTrackRenderSh2BusyLine(packet.sh2);
    if (packet.useSafeTelemetryFormat)
    {
        PresentTrackRenderSh2SimSafeLine(packet.sh2);
    }
    else
    {
        PresentTrackRenderSh2SimFallbackLine(packet.sh2,
                                             packet.slaveDispatchCount,
                                             packet.slaveDispatchSkipsTrackBusy);
    }
    PresentTrackRenderProducerStatePacket(packet.producerState);
}

} // namespace GameLoopObservabilityDomain
