#pragma once

#include "frame_reuse_observability_source_owner_assembler.hpp"
#include "game_loop_reuse_source_owner_contracts.hpp"
#include "game_loop_reuse_source_state_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedReuseObservabilitySourceOwnerPacket(
    const ReuseObservabilitySourcePacket& sourcePacket,
    ReuseObservabilitySourceOwnerPacket& outPacket)
{
    outPacket.valid = sourcePacket.hasSimulationDecision ||
                      sourcePacket.hasTrackDecision ||
                      sourcePacket.hasTelemetry;
    outPacket.source = sourcePacket;
}

inline ReuseObservabilitySourceOwnerPacket BuildReuseObservabilitySourceOwnerPacket(
    const ReuseObservabilitySourcePacket& sourcePacket)
{
    ReuseObservabilitySourceOwnerPacket packet{};
    SeedReuseObservabilitySourceOwnerPacket(sourcePacket, packet);
    return packet;
}

inline ReuseObservabilitySourceOwnerPacket BuildReuseObservabilitySourceOwnerPacket(
    const FrameReuseDomain::ReuseObservabilitySourceOwnerPacket& sourceOwnerPacket)
{
    return BuildReuseObservabilitySourceOwnerPacket(
        BuildReuseObservabilitySourcePacket(
            sourceOwnerPacket.source.hasSimulationDecision ? &sourceOwnerPacket.source.simulationDecision : nullptr,
            sourceOwnerPacket.source.hasTrackDecision ? &sourceOwnerPacket.source.trackDecision : nullptr,
            sourceOwnerPacket.source.hasTelemetry ? &sourceOwnerPacket.source.telemetry : nullptr));
}

inline ReuseObservabilitySourceOwnerPacket BuildReuseObservabilitySourceOwnerPacket(
    const FrameReuseDomain::SimulationReuseDecisionPacket* simulationDecision,
    const FrameReuseDomain::TrackReuseDecisionPacket* trackDecision,
    const FrameReuseDomain::FrameReuseTelemetry* telemetry)
{
    return BuildReuseObservabilitySourceOwnerPacket(
        FrameReuseDomain::BuildReuseObservabilitySourceOwnerPacket(simulationDecision,
                                                                   trackDecision,
                                                                   telemetry));
}

inline ReuseObservabilitySourceState CaptureReuseObservabilitySourceState(
    const ReuseObservabilitySourceOwnerPacket& ownerPacket)
{
    return CaptureReuseObservabilitySourceState(ownerPacket.source);
}

} // namespace GameLoopObservabilityDomain
