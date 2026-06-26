#pragma once

#include "game_loop_simulation_drain_view_contracts.hpp"
#include "simulation_scheduler_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedSimulationDrainViewPacket(const SimulationSchedulerDomain::SimulationDrainPacket& packet,
                                          SimulationDrainViewPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.mandatoryWait = packet.mandatoryWait;
    outPacket.softSpinLimit = packet.softSpinLimit;
    outPacket.hardSpinLimit = packet.hardSpinLimit;
}

inline SimulationDrainViewPacket BuildSimulationDrainViewPacket(
    const SimulationSchedulerDomain::SimulationDrainPacket& packet)
{
    SimulationDrainViewPacket outPacket{};
    SeedSimulationDrainViewPacket(packet, outPacket);
    return outPacket;
}

} // namespace GameLoopRuntime
