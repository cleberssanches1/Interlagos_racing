#pragma once

#include "game_loop_scheduler_reuse_simulation_preview_assembler.hpp"
#include "game_loop_simulation_reuse_runtime_debug_preview_presenter_ops.hpp"

namespace GameLoopObservabilityDomain
{

inline void PresentSchedulerReuseSimulationPreviewPacket(
    const SchedulerReuseSimulationPreviewPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    PresentSimulationReuseRuntimeDebugPreviewPacket(packet.simulationDebugPreview);
}

inline void PresentSchedulerReuseSimulationPreviewPacket(
    const SchedulerReuseFlowObservabilityPacket& flow,
    const GameLoopRuntime::SimulationReuseRuntimeDecisionInputsPacket& simulationInputs)
{
    PresentSchedulerReuseSimulationPreviewPacket(
        BuildSchedulerReuseSimulationPreviewPacket(flow, simulationInputs));
}

inline void PresentSchedulerReuseSimulationPreviewPacket(
    const SchedulerReuseFlowObservabilityPacket& flow,
    uint32_t requestFrameId,
    bool slaveSimulationEnabled,
    bool lockstepEnabled,
    bool jobInFlight,
    const FrameReuseDomain::SimulationFrameHistoryState& history)
{
    PresentSchedulerReuseSimulationPreviewPacket(
        BuildSchedulerReuseSimulationPreviewPacket(flow,
                                                   requestFrameId,
                                                   slaveSimulationEnabled,
                                                   lockstepEnabled,
                                                   jobInFlight,
                                                   history));
}

} // namespace GameLoopObservabilityDomain
