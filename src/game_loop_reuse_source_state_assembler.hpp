#pragma once

#include "game_loop_reuse_runtime_observability_ops.hpp"
#include "game_loop_reuse_source_state_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedReuseObservabilitySourcePacket(
    const FrameReuseDomain::SimulationReuseDecisionPacket* simulationDecision,
    const FrameReuseDomain::TrackReuseDecisionPacket* trackDecision,
    const FrameReuseDomain::FrameReuseTelemetry* telemetry,
    ReuseObservabilitySourcePacket& outPacket)
{
    outPacket = {};
    if (simulationDecision != nullptr)
    {
        outPacket.hasSimulationDecision = true;
        outPacket.simulationDecision = *simulationDecision;
    }
    if (trackDecision != nullptr)
    {
        outPacket.hasTrackDecision = true;
        outPacket.trackDecision = *trackDecision;
    }
    if (telemetry != nullptr)
    {
        outPacket.hasTelemetry = true;
        outPacket.telemetry = *telemetry;
    }
}

inline ReuseObservabilitySourcePacket BuildReuseObservabilitySourcePacket(
    const FrameReuseDomain::SimulationReuseDecisionPacket* simulationDecision,
    const FrameReuseDomain::TrackReuseDecisionPacket* trackDecision,
    const FrameReuseDomain::FrameReuseTelemetry* telemetry)
{
    ReuseObservabilitySourcePacket packet{};
    SeedReuseObservabilitySourcePacket(simulationDecision,
                                       trackDecision,
                                       telemetry,
                                       packet);
    return packet;
}

inline ReuseObservabilitySourceState CaptureReuseObservabilitySourceState(
    const ReuseObservabilitySourcePacket& packet)
{
    ReuseObservabilitySourceState outState{};
    outState.simulationDecision =
        packet.hasSimulationDecision ? &packet.simulationDecision : nullptr;
    outState.trackDecision =
        packet.hasTrackDecision ? &packet.trackDecision : nullptr;
    outState.telemetry =
        packet.hasTelemetry ? &packet.telemetry : nullptr;
    return outState;
}

} // namespace GameLoopObservabilityDomain
