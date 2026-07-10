#pragma once

#include "game_loop_simulation_reuse_runtime_preview_bridge_presenter_ops.hpp"

namespace GameLoopObservabilityDomain
{

inline void PresentSimulationReuseRuntimeDebugPreview(
    const GameLoopRuntime::SimulationReuseRuntimeDecisionInputsPacket& inputs)
{
    GameLoopRuntime::PresentSimulationReuseRuntimePreviewPacket(inputs);
}

inline void PresentSimulationReuseRuntimeDebugPreview(
    uint32_t requestFrameId,
    bool slaveSimulationEnabled,
    bool lockstepEnabled,
    bool jobInFlight,
    const FrameReuseDomain::SimulationFrameHistoryState& history)
{
    GameLoopRuntime::PresentSimulationReuseRuntimePreviewPacket(requestFrameId,
                                                                slaveSimulationEnabled,
                                                                lockstepEnabled,
                                                                jobInFlight,
                                                                history);
}

} // namespace GameLoopObservabilityDomain
