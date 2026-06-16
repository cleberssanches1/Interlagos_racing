#pragma once

#include "simulation_scheduler_contracts.hpp"

// Assemblers passivos do recorte do scheduler de simulacao.
// Nao participam do runtime atual; apenas preparam a futura extracao.

namespace SimulationSchedulerDomain
{

inline void SeedSimulationFrameContext(const Game::GameplayFrameState& frameState,
                                       const Game::SimulationSchedulerPolicy& policy,
                                       Game::SimulationDispatchMode dispatchMode,
                                       bool slaveSimulationEnabled,
                                       bool slaveLockstep,
                                       bool trackProducerBusy,
                                       bool carPrepareBusy,
                                       SimulationFrameContext& outContext)
{
    outContext.frameId = frameState.frameId;
    outContext.frameState = frameState;
    outContext.policy = policy;
    outContext.dispatchMode = dispatchMode;
    outContext.slaveSimulationEnabled = slaveSimulationEnabled;
    outContext.slaveLockstep = slaveLockstep;
    outContext.trackProducerBusy = trackProducerBusy;
    outContext.carPrepareBusy = carPrepareBusy;
}

inline void SeedSimulationDispatchPacket(const SimulationFrameContext& context,
                                         const Game::SimulationRuntimeState& runtimeState,
                                         SimulationDispatchPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.useSlave =
        context.slaveSimulationEnabled &&
        (context.dispatchMode != Game::SimulationDispatchMode::Synchronous);
    outPacket.blockedByBackoff = (runtimeState.slaveBackoffFrames > 0u);
    outPacket.blockedByTrackBusy = context.trackProducerBusy;
    outPacket.blockedByInFlightJob = runtimeState.JobInFlight();
    outPacket.blockedByCarPrepare = context.carPrepareBusy;
    outPacket.requiresLockstepDrain =
        outPacket.useSlave && context.slaveLockstep;
    outPacket.targetSlot = runtimeState.writeIdx;
    outPacket.payload.frameState = context.frameState;
}

inline bool CanDispatchSimulationPacket(const SimulationDispatchPacket& packet)
{
    if (!packet.valid || !packet.useSlave) return false;
    if (packet.blockedByBackoff) return false;
    if (packet.blockedByTrackBusy) return false;
    if (packet.blockedByInFlightJob) return false;
    if (packet.blockedByCarPrepare) return false;
    return true;
}

inline void SeedSimulationDrainPacket(const Game::SimulationSchedulerPolicy& policy,
                                      bool mandatoryWait,
                                      SimulationDrainPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.mandatoryWait = mandatoryWait;
    outPacket.softSpinLimit = policy.softSpinLimit;
    outPacket.hardSpinLimit = policy.hardSpinLimit;
}

} // namespace SimulationSchedulerDomain
