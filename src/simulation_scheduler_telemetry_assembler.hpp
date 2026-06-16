#pragma once

#include "simulation_scheduler_contracts.hpp"

// Assembler passivo de completion e telemetria do scheduler de simulacao.
// Mantem o recorte fora de `GameLoopSystem` ate futura integracao.

namespace SimulationSchedulerDomain
{

inline void SeedSimulationCompletionPacket(const Game::SimulationRuntimeState& runtimeState,
                                           SimulationCompletionPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.jobInFlight = runtimeState.JobInFlight();
    outPacket.hasCompleted = runtimeState.HasCompleted();
    outPacket.inFlightIdx = runtimeState.inFlightIdx;
    outPacket.completedIdx = runtimeState.completedIdx;
}

inline void SeedSimulationSchedulerTelemetry(const Game::SimulationRuntimeState& runtimeState,
                                             SimulationSchedulerTelemetry& outTelemetry)
{
    outTelemetry.slaveDispatchCount = runtimeState.slaveDispatchCount;
    outTelemetry.slaveDispatchSkipsTrackBusy = runtimeState.slaveDispatchSkipsTrackBusy;
    outTelemetry.slaveDispatchSkipsBackoff = runtimeState.slaveDispatchSkipsBackoff;
    outTelemetry.drainSoftTimeouts = runtimeState.drainSoftTimeouts;
    outTelemetry.drainHardWaits = runtimeState.drainHardWaits;
    outTelemetry.masterWaitTicksThisFrame = runtimeState.masterWaitTicksThisFrame;
    outTelemetry.slaveLastJobTicksThisFrame = runtimeState.slaveLastJobTicksThisFrame;
    outTelemetry.slaveBackoffFrames = runtimeState.slaveBackoffFrames;
    outTelemetry.jobInFlight = runtimeState.JobInFlight();
    outTelemetry.hasCompleted = runtimeState.HasCompleted();
}

} // namespace SimulationSchedulerDomain
