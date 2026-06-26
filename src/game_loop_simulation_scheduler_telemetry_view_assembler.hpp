#pragma once

#include "game_loop_simulation_scheduler_telemetry_view_contracts.hpp"
#include "simulation_scheduler_contracts.hpp"

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

inline SimulationSchedulerTelemetryViewPacket BuildSimulationSchedulerTelemetryViewPacket(
    const SimulationSchedulerDomain::SimulationSchedulerTelemetry& telemetry)
{
    SimulationSchedulerTelemetryViewPacket packet{};
    SeedSimulationSchedulerTelemetryViewPacket(telemetry, packet);
    return packet;
}

} // namespace GameLoopRuntime
