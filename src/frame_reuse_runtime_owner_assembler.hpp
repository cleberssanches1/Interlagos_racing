#pragma once

#include "frame_reuse_runtime_owner_contracts.hpp"

namespace FrameReuseDomain
{

inline void SeedFrameReuseRuntimeOwnerPacket(
    const SimulationFrameHistoryState* simulationHistory,
    const TrackFrameHistoryState* trackHistory,
    const SimulationReuseDecisionPacket* simulationDecision,
    const TrackReuseDecisionPacket* trackDecision,
    const FrameReuseTelemetry* telemetry,
    FrameReuseRuntimeOwnerPacket& outPacket)
{
    outPacket = {};
    if (simulationHistory != nullptr)
    {
        outPacket.hasSimulationHistory = true;
        outPacket.simulationHistory = *simulationHistory;
    }
    if (trackHistory != nullptr)
    {
        outPacket.hasTrackHistory = true;
        outPacket.trackHistory = *trackHistory;
    }
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

inline FrameReuseRuntimeOwnerPacket BuildFrameReuseRuntimeOwnerPacket(
    const SimulationFrameHistoryState* simulationHistory,
    const TrackFrameHistoryState* trackHistory,
    const SimulationReuseDecisionPacket* simulationDecision,
    const TrackReuseDecisionPacket* trackDecision,
    const FrameReuseTelemetry* telemetry)
{
    FrameReuseRuntimeOwnerPacket packet{};
    SeedFrameReuseRuntimeOwnerPacket(simulationHistory,
                                     trackHistory,
                                     simulationDecision,
                                     trackDecision,
                                     telemetry,
                                     packet);
    return packet;
}

} // namespace FrameReuseDomain
