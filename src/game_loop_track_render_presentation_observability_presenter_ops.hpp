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

    SRL::Debug::Print(1, 24, "S2 %u/%u m:%u s:%u",
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

    SRL::Debug::Print(1, 25, "S2 %u %u q:%u g:%u c:%u",
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

    SRL::Debug::Print(1, 25, "S2 %u %u %u%% d:%u b:%u",
                      static_cast<unsigned>(snapshot.simSlaveTicks),
                      static_cast<unsigned>(snapshot.masterWaitTicks),
                      static_cast<unsigned>(snapshot.masterWaitPctOfSim),
                      static_cast<unsigned>(slaveDispatchCount),
                      static_cast<unsigned>(slaveDispatchSkipsTrackBusy));
}

inline void PresentTrackRenderProducerStateFlags(bool hasProducerState,
                                                 bool producerJobInFlight,
                                                 bool producerSafeModeActive)
{
    if (!hasProducerState)
    {
        return;
    }

    SRL::Debug::Print(1, 26, "S2 p:%u s:%u",
                      static_cast<unsigned>(producerJobInFlight ? 1u : 0u),
                      static_cast<unsigned>(producerSafeModeActive ? 1u : 0u));
}

inline void PresentTrackRenderPresentationObservabilityPacket(
    const TrackRenderPresentationObservabilityPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    PresentTrackRenderSh2BusyLine(packet.sh2);
    PresentTrackRenderProducerStateFlags(packet.hasProducerState,
                                         packet.producerJobInFlight,
                                         packet.producerSafeModeActive);
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
    PresentTrackRenderProducerStateFlags(packet.hasProducerState,
                                         packet.producerJobInFlight,
                                         packet.producerSafeModeActive);
}

} // namespace GameLoopObservabilityDomain
