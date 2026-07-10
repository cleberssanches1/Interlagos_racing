#pragma once

#include "game_loop_scheduler_reuse_simulation_preview_contracts.hpp"
#include "game_loop_simulation_reuse_runtime_debug_preview_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedSchedulerReuseSimulationPreviewPacket(
    const SchedulerReuseFlowObservabilityPacket& flow,
    const SimulationReuseRuntimeDebugPreviewPacket& simulationDebugPreview,
    SchedulerReuseSimulationPreviewPacket& outPacket)
{
    outPacket.valid = flow.valid || simulationDebugPreview.valid;
    outPacket.flow = flow;
    outPacket.simulationDebugPreview = simulationDebugPreview;
}

inline SchedulerReuseSimulationPreviewPacket BuildSchedulerReuseSimulationPreviewPacket(
    const SchedulerReuseFlowObservabilityPacket& flow,
    const SimulationReuseRuntimeDebugPreviewPacket& simulationDebugPreview)
{
    SchedulerReuseSimulationPreviewPacket packet{};
    SeedSchedulerReuseSimulationPreviewPacket(flow, simulationDebugPreview, packet);
    return packet;
}

inline SchedulerReuseSimulationPreviewPacket BuildSchedulerReuseSimulationPreviewPacket(
    const SchedulerReuseFlowObservabilityPacket& flow,
    const GameLoopRuntime::SimulationReuseRuntimeDecisionInputsPacket& simulationInputs)
{
    return BuildSchedulerReuseSimulationPreviewPacket(
        flow,
        BuildSimulationReuseRuntimeDebugPreviewPacket(simulationInputs));
}

inline SchedulerReuseSimulationPreviewPacket BuildSchedulerReuseSimulationPreviewPacket(
    const SchedulerReuseFlowObservabilityPacket& flow,
    uint32_t requestFrameId,
    bool slaveSimulationEnabled,
    bool lockstepEnabled,
    bool jobInFlight,
    const FrameReuseDomain::SimulationFrameHistoryState& history)
{
    return BuildSchedulerReuseSimulationPreviewPacket(
        flow,
        BuildSimulationReuseRuntimeDebugPreviewPacket(requestFrameId,
                                                     slaveSimulationEnabled,
                                                     lockstepEnabled,
                                                     jobInFlight,
                                                     history));
}

} // namespace GameLoopObservabilityDomain
