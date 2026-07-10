#pragma once

#include "game_loop_scheduler_reuse_observability_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline bool HasSimulationSchedulerTelemetryViewSignal(
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& telemetry)
{
    return telemetry.slaveDispatchCount > 0u ||
           telemetry.slaveDispatchSkipsTrackBusy > 0u ||
           telemetry.slaveDispatchSkipsBackoff > 0u ||
           telemetry.drainSoftTimeouts > 0u ||
           telemetry.drainHardWaits > 0u ||
           telemetry.masterWaitTicksThisFrame > 0u ||
           telemetry.slaveLastJobTicksThisFrame > 0u ||
           telemetry.slaveBackoffFrames > 0u ||
           telemetry.jobInFlight ||
           telemetry.hasCompleted;
}

inline void SeedSchedulerReuseObservabilityPacket(
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const ReuseObservabilityPacket& reuse,
    SchedulerReuseObservabilityPacket& outPacket)
{
    outPacket.valid = HasSimulationSchedulerTelemetryViewSignal(schedulerTelemetry) ||
                      hasProducerState ||
                      reuse.valid;
    outPacket.schedulerTelemetry = schedulerTelemetry;
    outPacket.hasProducerState = hasProducerState;
    outPacket.producerJobInFlight = producerJobInFlight;
    outPacket.producerSafeModeActive = producerSafeModeActive;
    outPacket.reuse = reuse;
}

inline SchedulerReuseObservabilityPacket BuildSchedulerReuseObservabilityPacket(
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const ReuseObservabilityPacket& reuse)
{
    SchedulerReuseObservabilityPacket packet{};
    SeedSchedulerReuseObservabilityPacket(schedulerTelemetry,
                                          hasProducerState,
                                          producerJobInFlight,
                                          producerSafeModeActive,
                                          reuse,
                                          packet);
    return packet;
}

inline SchedulerReuseObservabilityPacket BuildSchedulerReuseObservabilityPacket(
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const ReuseObservabilityPacket& reuse)
{
    return BuildSchedulerReuseObservabilityPacket(schedulerTelemetry,
                                                  true,
                                                  producerJobInFlight,
                                                  producerSafeModeActive,
                                                  reuse);
}

inline SchedulerReuseObservabilityPacket BuildSchedulerReuseObservabilityPacket(
    const SchedulerReuseObservabilityAssemblyInputs& inputs)
{
    return BuildSchedulerReuseObservabilityPacket(inputs.schedulerTelemetry,
                                                  inputs.hasProducerState,
                                                  inputs.producerJobInFlight,
                                                  inputs.producerSafeModeActive,
                                                  inputs.reuse);
}

} // namespace GameLoopObservabilityDomain
