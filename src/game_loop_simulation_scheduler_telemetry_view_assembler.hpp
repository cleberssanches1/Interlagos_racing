#pragma once

#include "game_loop_simulation_scheduler_telemetry_view_contracts.hpp"
#include "simulation_scheduler_contracts.hpp"
#include "simulation_scheduler_state.hpp"

namespace GameLoopRuntime
{

inline void SeedSimulationSchedulerTelemetryViewPacket(
    const SimulationSchedulerDomain::SimulationSchedulerTelemetry& telemetry,
    SimulationSchedulerTelemetryViewPacket& outPacket)
{
    outPacket.slaveDispatchCount = telemetry.slaveDispatchCount;
    outPacket.slaveDispatchSkipsTrackBusy = telemetry.slaveDispatchSkipsTrackBusy;
    outPacket.slaveDispatchSkipsBackoff = telemetry.slaveDispatchSkipsBackoff;
    outPacket.drainSoftTimeouts = telemetry.drainSoftTimeouts;
    outPacket.drainHardWaits = telemetry.drainHardWaits;
    outPacket.masterWaitTicksThisFrame = telemetry.masterWaitTicksThisFrame;
    outPacket.slaveLastJobTicksThisFrame = telemetry.slaveLastJobTicksThisFrame;
    outPacket.slaveBackoffFrames = telemetry.slaveBackoffFrames;
    outPacket.jobInFlight = telemetry.jobInFlight;
    outPacket.hasCompleted = telemetry.hasCompleted;
}

inline void SeedSimulationSchedulerTelemetryViewPacket(
    const Game::SimulationRuntimeState& runtimeState,
    SimulationSchedulerTelemetryViewPacket& outPacket)
{
    outPacket.slaveDispatchCount = runtimeState.slaveDispatchCount;
    outPacket.slaveDispatchSkipsTrackBusy = runtimeState.slaveDispatchSkipsTrackBusy;
    outPacket.slaveDispatchSkipsBackoff = runtimeState.slaveDispatchSkipsBackoff;
    outPacket.drainSoftTimeouts = runtimeState.drainSoftTimeouts;
    outPacket.drainHardWaits = runtimeState.drainHardWaits;
    outPacket.masterWaitTicksThisFrame = runtimeState.masterWaitTicksThisFrame;
    outPacket.slaveLastJobTicksThisFrame = runtimeState.slaveLastJobTicksThisFrame;
    outPacket.slaveBackoffFrames = runtimeState.slaveBackoffFrames;
    outPacket.jobInFlight = runtimeState.JobInFlight();
    outPacket.hasCompleted = runtimeState.HasCompleted();
}

inline SimulationSchedulerTelemetryViewPacket BuildSimulationSchedulerTelemetryViewPacket(
    const SimulationSchedulerDomain::SimulationSchedulerTelemetry& telemetry)
{
    SimulationSchedulerTelemetryViewPacket packet{};
    SeedSimulationSchedulerTelemetryViewPacket(telemetry, packet);
    return packet;
}

inline SimulationSchedulerTelemetryViewPacket BuildSimulationSchedulerTelemetryViewPacket(
    const Game::SimulationRuntimeState& runtimeState)
{
    SimulationSchedulerTelemetryViewPacket packet{};
    SeedSimulationSchedulerTelemetryViewPacket(runtimeState, packet);
    return packet;
}

} // namespace GameLoopRuntime
