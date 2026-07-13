#pragma once

#include "frame_reuse_runtime_owner_contracts.hpp"
#include "game_loop_reuse_source_state_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline ReuseObservabilitySourcePacket BuildReuseObservabilitySourcePacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildReuseObservabilitySourcePacket(
        runtimeOwnerPacket.hasSimulationDecision ? &runtimeOwnerPacket.simulationDecision : nullptr,
        runtimeOwnerPacket.hasTrackDecision ? &runtimeOwnerPacket.trackDecision : nullptr,
        runtimeOwnerPacket.hasTelemetry ? &runtimeOwnerPacket.telemetry : nullptr);
}

} // namespace GameLoopObservabilityDomain
