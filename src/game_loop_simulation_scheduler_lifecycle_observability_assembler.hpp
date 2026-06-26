#pragma once

#include "game_loop_simulation_scheduler_lifecycle_observability_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedSimulationSchedulerLifecycleObservabilityPacket(
    const GameLoopRuntime::SimulationDrainViewPacket& drain,
    const GameLoopRuntime::SimulationCompletionViewPacket& completion,
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& telemetry,
    SimulationSchedulerLifecycleObservabilityPacket& outPacket)
{
    outPacket.valid = drain.valid ||
                      completion.valid ||
                      telemetry.slaveDispatchCount > 0u ||
                      telemetry.slaveDispatchSkipsTrackBusy > 0u ||
                      telemetry.slaveDispatchSkipsBackoff > 0u ||
                      telemetry.drainSoftTimeouts > 0u ||
                      telemetry.drainHardWaits > 0u ||
                      telemetry.masterWaitTicksThisFrame > 0u ||
                      telemetry.slaveLastJobTicksThisFrame > 0u ||
                      telemetry.slaveBackoffFrames > 0u ||
                      telemetry.jobInFlight ||
                      telemetry.hasCompleted;
    outPacket.drain = drain;
    outPacket.completion = completion;
    outPacket.telemetry = telemetry;
}

inline SimulationSchedulerLifecycleObservabilityPacket BuildSimulationSchedulerLifecycleObservabilityPacket(
    const GameLoopRuntime::SimulationDrainViewPacket& drain,
    const GameLoopRuntime::SimulationCompletionViewPacket& completion,
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& telemetry)
{
    SimulationSchedulerLifecycleObservabilityPacket outPacket{};
    SeedSimulationSchedulerLifecycleObservabilityPacket(drain, completion, telemetry, outPacket);
    return outPacket;
}

} // namespace GameLoopObservabilityDomain
