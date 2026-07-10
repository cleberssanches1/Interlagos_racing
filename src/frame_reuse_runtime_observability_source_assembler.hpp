#pragma once

#include "frame_reuse_observability_source_assembler.hpp"
#include "frame_reuse_runtime_owner_assembler.hpp"

namespace FrameReuseDomain
{

inline ReuseObservabilitySourceSnapshot
BuildReuseObservabilitySourceSnapshot(const FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildReuseObservabilitySourceSnapshot(
        runtimeOwnerPacket.hasSimulationDecision ? &runtimeOwnerPacket.simulationDecision : nullptr,
        runtimeOwnerPacket.hasTrackDecision ? &runtimeOwnerPacket.trackDecision : nullptr,
        runtimeOwnerPacket.hasTelemetry ? &runtimeOwnerPacket.telemetry : nullptr);
}

} // namespace FrameReuseDomain
