#pragma once

#include "frame_reuse_runtime_observability_source_assembler.hpp"
#include "game_loop_reuse_source_state_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline ReuseObservabilitySourcePacket BuildReuseObservabilitySourcePacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    const auto sourceSnapshot =
        FrameReuseDomain::BuildReuseObservabilitySourceSnapshot(runtimeOwnerPacket);
    return BuildReuseObservabilitySourcePacket(
        sourceSnapshot.hasSimulationDecision ? &sourceSnapshot.simulationDecision : nullptr,
        sourceSnapshot.hasTrackDecision ? &sourceSnapshot.trackDecision : nullptr,
        sourceSnapshot.hasTelemetry ? &sourceSnapshot.telemetry : nullptr);
}

} // namespace GameLoopObservabilityDomain
