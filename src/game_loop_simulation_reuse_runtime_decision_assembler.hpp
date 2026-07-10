#pragma once

#include "game_loop_simulation_reuse_runtime_decision_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedSimulationReuseRuntimeDecisionInputsPacket(
    uint32_t requestFrameId,
    bool slaveSimulationEnabled,
    bool lockstepEnabled,
    bool jobInFlight,
    const FrameReuseDomain::SimulationFrameHistoryState& history,
    SimulationReuseRuntimeDecisionInputsPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.requestFrameId = requestFrameId;
    outPacket.slaveSimulationEnabled = slaveSimulationEnabled;
    outPacket.lockstepEnabled = lockstepEnabled;
    outPacket.jobInFlight = jobInFlight;
    outPacket.history = history;
}

inline SimulationReuseRuntimeDecisionInputsPacket
BuildSimulationReuseRuntimeDecisionInputsPacket(
    uint32_t requestFrameId,
    bool slaveSimulationEnabled,
    bool lockstepEnabled,
    bool jobInFlight,
    const FrameReuseDomain::SimulationFrameHistoryState& history)
{
    SimulationReuseRuntimeDecisionInputsPacket packet{};
    SeedSimulationReuseRuntimeDecisionInputsPacket(requestFrameId,
                                                   slaveSimulationEnabled,
                                                   lockstepEnabled,
                                                   jobInFlight,
                                                   history,
                                                   packet);
    return packet;
}

} // namespace GameLoopRuntime
