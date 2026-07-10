#pragma once

#include "game_loop_simulation_reuse_runtime_debug_preview_assembler.hpp"
#include "game_loop_simulation_reuse_runtime_preview_presenter_ops.hpp"

namespace GameLoopObservabilityDomain
{

inline void PresentSimulationReuseRuntimeDebugPreviewPacket(
    const SimulationReuseRuntimeDebugPreviewPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    GameLoopRuntime::PresentSimulationReuseRuntimePreviewPacket(packet.preview);
}

inline void PresentSimulationReuseRuntimeDebugPreviewPacket(
    const GameLoopRuntime::SimulationReuseRuntimeDecisionInputsPacket& inputs)
{
    PresentSimulationReuseRuntimeDebugPreviewPacket(
        BuildSimulationReuseRuntimeDebugPreviewPacket(inputs));
}

inline void PresentSimulationReuseRuntimeDebugPreviewPacket(
    uint32_t requestFrameId,
    bool slaveSimulationEnabled,
    bool lockstepEnabled,
    bool jobInFlight,
    const FrameReuseDomain::SimulationFrameHistoryState& history)
{
    PresentSimulationReuseRuntimeDebugPreviewPacket(
        BuildSimulationReuseRuntimeDebugPreviewPacket(requestFrameId,
                                                      slaveSimulationEnabled,
                                                      lockstepEnabled,
                                                      jobInFlight,
                                                      history));
}

} // namespace GameLoopObservabilityDomain
