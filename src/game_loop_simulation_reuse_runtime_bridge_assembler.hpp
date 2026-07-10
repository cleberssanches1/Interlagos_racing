#pragma once

#include "game_loop_simulation_reuse_decision_view_assembler.hpp"
#include "game_loop_simulation_reuse_runtime_decision_assembler.hpp"
#include "simulation_frame_reuse_ops.hpp"

namespace GameLoopRuntime
{

inline FrameReuseDomain::SimulationReuseDecisionPacket
BuildSimulationReuseRuntimeDecisionPacket(
    const SimulationReuseRuntimeDecisionInputsPacket& inputs)
{
    FrameReuseDomain::SimulationReuseDecisionPacket packet{};
    if (!inputs.valid)
    {
        return packet;
    }

    FrameReuseDomain::SeedSimulationReuseDecisionPacket(inputs.requestFrameId,
                                                        inputs.slaveSimulationEnabled,
                                                        inputs.lockstepEnabled,
                                                        inputs.jobInFlight,
                                                        inputs.history,
                                                        packet);
    return packet;
}

inline SimulationReuseDecisionViewPacket BuildSimulationReuseRuntimeDecisionViewPacket(
    const SimulationReuseRuntimeDecisionInputsPacket& inputs)
{
    return BuildSimulationReuseDecisionViewPacket(
        BuildSimulationReuseRuntimeDecisionPacket(inputs));
}

} // namespace GameLoopRuntime
