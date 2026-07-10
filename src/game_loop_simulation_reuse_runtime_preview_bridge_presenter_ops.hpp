#pragma once

#include "game_loop_simulation_reuse_runtime_preview_assembler.hpp"
#include "game_loop_simulation_reuse_runtime_preview_presenter_ops.hpp"

namespace GameLoopRuntime
{

inline void PresentSimulationReuseRuntimePreviewPacket(
    const SimulationReuseRuntimeDecisionInputsPacket& inputs)
{
    PresentSimulationReuseRuntimePreviewPacket(
        BuildSimulationReuseRuntimePreviewPacket(inputs));
}

inline void PresentSimulationReuseRuntimePreviewPacket(
    uint32_t requestFrameId,
    bool slaveSimulationEnabled,
    bool lockstepEnabled,
    bool jobInFlight,
    const FrameReuseDomain::SimulationFrameHistoryState& history)
{
    PresentSimulationReuseRuntimePreviewPacket(
        BuildSimulationReuseRuntimePreviewPacket(requestFrameId,
                                                 slaveSimulationEnabled,
                                                 lockstepEnabled,
                                                 jobInFlight,
                                                 history));
}

} // namespace GameLoopRuntime
