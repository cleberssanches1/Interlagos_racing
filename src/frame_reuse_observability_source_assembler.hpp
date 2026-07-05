#pragma once

#include "frame_reuse_observability_source_contracts.hpp"

namespace FrameReuseDomain
{

inline void SeedReuseObservabilitySourceSnapshot(
    const SimulationReuseDecisionPacket* simulationDecision,
    const TrackReuseDecisionPacket* trackDecision,
    const FrameReuseTelemetry* telemetry,
    ReuseObservabilitySourceSnapshot& outSnapshot)
{
    outSnapshot = {};
    if (simulationDecision != nullptr)
    {
        outSnapshot.hasSimulationDecision = true;
        outSnapshot.simulationDecision = *simulationDecision;
    }
    if (trackDecision != nullptr)
    {
        outSnapshot.hasTrackDecision = true;
        outSnapshot.trackDecision = *trackDecision;
    }
    if (telemetry != nullptr)
    {
        outSnapshot.hasTelemetry = true;
        outSnapshot.telemetry = *telemetry;
    }
}

inline ReuseObservabilitySourceSnapshot BuildReuseObservabilitySourceSnapshot(
    const SimulationReuseDecisionPacket* simulationDecision,
    const TrackReuseDecisionPacket* trackDecision,
    const FrameReuseTelemetry* telemetry)
{
    ReuseObservabilitySourceSnapshot snapshot{};
    SeedReuseObservabilitySourceSnapshot(simulationDecision,
                                         trackDecision,
                                         telemetry,
                                         snapshot);
    return snapshot;
}

} // namespace FrameReuseDomain
