#pragma once

#include "frame_reuse_observability_source_assembler.hpp"
#include "frame_reuse_observability_source_owner_contracts.hpp"

namespace FrameReuseDomain
{

inline void SeedReuseObservabilitySourceOwnerPacket(
    const ReuseObservabilitySourceSnapshot& sourceSnapshot,
    ReuseObservabilitySourceOwnerPacket& outPacket)
{
    outPacket.valid = sourceSnapshot.hasSimulationDecision ||
                      sourceSnapshot.hasTrackDecision ||
                      sourceSnapshot.hasTelemetry;
    outPacket.source = sourceSnapshot;
}

inline ReuseObservabilitySourceOwnerPacket BuildReuseObservabilitySourceOwnerPacket(
    const ReuseObservabilitySourceSnapshot& sourceSnapshot)
{
    ReuseObservabilitySourceOwnerPacket packet{};
    SeedReuseObservabilitySourceOwnerPacket(sourceSnapshot, packet);
    return packet;
}

inline ReuseObservabilitySourceOwnerPacket BuildReuseObservabilitySourceOwnerPacket(
    const SimulationReuseDecisionPacket* simulationDecision,
    const TrackReuseDecisionPacket* trackDecision,
    const FrameReuseTelemetry* telemetry)
{
    return BuildReuseObservabilitySourceOwnerPacket(
        BuildReuseObservabilitySourceSnapshot(simulationDecision,
                                             trackDecision,
                                             telemetry));
}

} // namespace FrameReuseDomain
