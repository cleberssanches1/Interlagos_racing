#pragma once

#include "frame_reuse_runtime_observability_source_assembler.hpp"
#include "frame_reuse_runtime_observability_owner_assembler.hpp"

namespace FrameReuseDomain
{

inline ReuseObservabilitySourceSnapshot CaptureEmptyReuseObservabilitySourceSnapshot()
{
    return {};
}

inline ReuseObservabilitySourceSnapshot CaptureReuseObservabilitySourceSnapshot(
    const SimulationReuseDecisionPacket* simulationDecision,
    const TrackReuseDecisionPacket* trackDecision,
    const FrameReuseTelemetry* telemetry)
{
    return BuildReuseObservabilitySourceSnapshot(simulationDecision,
                                                 trackDecision,
                                                 telemetry);
}

inline ReuseObservabilitySourceSnapshot CaptureReuseObservabilitySourceSnapshot(
    const FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildReuseObservabilitySourceSnapshot(runtimeOwnerPacket);
}

inline ReuseObservabilitySourceOwnerPacket CaptureEmptyReuseObservabilitySourceOwnerPacket()
{
    return {};
}

inline ReuseObservabilitySourceOwnerPacket CaptureReuseObservabilitySourceOwnerPacket(
    const ReuseObservabilitySourceSnapshot& sourceSnapshot)
{
    return BuildReuseObservabilitySourceOwnerPacket(sourceSnapshot);
}

inline ReuseObservabilitySourceOwnerPacket CaptureReuseObservabilitySourceOwnerPacket(
    const FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildReuseObservabilitySourceOwnerPacket(runtimeOwnerPacket);
}

inline ReuseObservabilitySourceOwnerPacket CaptureReuseObservabilitySourceOwnerPacket(
    const SimulationReuseDecisionPacket* simulationDecision,
    const TrackReuseDecisionPacket* trackDecision,
    const FrameReuseTelemetry* telemetry)
{
    return BuildReuseObservabilitySourceOwnerPacket(simulationDecision,
                                                    trackDecision,
                                                    telemetry);
}

inline FrameReuseRuntimeOwnerPacket CaptureEmptyFrameReuseRuntimeOwnerPacket()
{
    return {};
}

inline FrameReuseRuntimeOwnerPacket CaptureFrameReuseRuntimeOwnerPacket(
    const SimulationFrameHistoryState* simulationHistory,
    const TrackFrameHistoryState* trackHistory,
    const SimulationReuseDecisionPacket* simulationDecision,
    const TrackReuseDecisionPacket* trackDecision,
    const FrameReuseTelemetry* telemetry)
{
    return BuildFrameReuseRuntimeOwnerPacket(simulationHistory,
                                             trackHistory,
                                             simulationDecision,
                                             trackDecision,
                                             telemetry);
}

} // namespace FrameReuseDomain
