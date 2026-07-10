#pragma once

#include "game_loop_simulation_reuse_runtime_bridge_assembler.hpp"
#include "game_loop_simulation_reuse_runtime_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedSimulationReuseRuntimePreviewPacket(
    const SimulationReuseRuntimeDecisionInputsPacket& inputs,
    SimulationReuseRuntimePreviewPacket& outPacket)
{
    outPacket.valid = inputs.valid;
    outPacket.inputs = inputs;
    outPacket.decisionView = BuildSimulationReuseRuntimeDecisionViewPacket(inputs);
}

inline SimulationReuseRuntimePreviewPacket BuildSimulationReuseRuntimePreviewPacket(
    const SimulationReuseRuntimeDecisionInputsPacket& inputs)
{
    SimulationReuseRuntimePreviewPacket packet{};
    SeedSimulationReuseRuntimePreviewPacket(inputs, packet);
    return packet;
}

inline SimulationReuseRuntimePreviewPacket BuildSimulationReuseRuntimePreviewPacket(
    uint32_t requestFrameId,
    bool slaveSimulationEnabled,
    bool lockstepEnabled,
    bool jobInFlight,
    const FrameReuseDomain::SimulationFrameHistoryState& history)
{
    return BuildSimulationReuseRuntimePreviewPacket(
        BuildSimulationReuseRuntimeDecisionInputsPacket(requestFrameId,
                                                        slaveSimulationEnabled,
                                                        lockstepEnabled,
                                                        jobInFlight,
                                                        history));
}

} // namespace GameLoopRuntime
