#pragma once

#include "frame_reuse_observability_source_owner_assembler.hpp"
#include "frame_reuse_runtime_owner_assembler.hpp"

namespace FrameReuseDomain
{

inline ReuseObservabilitySourceOwnerPacket
BuildReuseObservabilitySourceOwnerPacket(const FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildReuseObservabilitySourceOwnerPacket(
        runtimeOwnerPacket.hasSimulationDecision ? &runtimeOwnerPacket.simulationDecision : nullptr,
        runtimeOwnerPacket.hasTrackDecision ? &runtimeOwnerPacket.trackDecision : nullptr,
        runtimeOwnerPacket.hasTelemetry ? &runtimeOwnerPacket.telemetry : nullptr);
}

} // namespace FrameReuseDomain
