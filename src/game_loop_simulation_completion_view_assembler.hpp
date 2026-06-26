#pragma once

#include "game_loop_simulation_completion_view_contracts.hpp"
#include "simulation_scheduler_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedSimulationCompletionViewPacket(
    const SimulationSchedulerDomain::SimulationCompletionPacket& packet,
    SimulationCompletionViewPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.jobInFlight = packet.jobInFlight;
    outPacket.hasCompleted = packet.hasCompleted;
    outPacket.inFlightIdx = packet.inFlightIdx;
    outPacket.completedIdx = packet.completedIdx;
}

inline SimulationCompletionViewPacket BuildSimulationCompletionViewPacket(
    const SimulationSchedulerDomain::SimulationCompletionPacket& packet)
{
    SimulationCompletionViewPacket outPacket{};
    SeedSimulationCompletionViewPacket(packet, outPacket);
    return outPacket;
}

} // namespace GameLoopRuntime
