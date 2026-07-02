#pragma once

#include "game_loop_scheduler_reuse_observability_contracts.hpp"
#include "game_loop_track_render_producer_state_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedSchedulerReuseObservabilityPacket(
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& simulationScheduler,
    bool hasTrackProducerState,
    bool trackProducerJobInFlight,
    bool trackProducerSafeModeActive,
    const ReuseObservabilityPacket& reuse,
    SchedulerReuseObservabilityPacket& outPacket)
{
    outPacket.valid = reuse.valid ||
                      simulationScheduler.slaveDispatchCount > 0u ||
                      simulationScheduler.slaveDispatchSkipsTrackBusy > 0u ||
                      simulationScheduler.slaveDispatchSkipsBackoff > 0u ||
                      simulationScheduler.drainSoftTimeouts > 0u ||
                      simulationScheduler.drainHardWaits > 0u ||
                      simulationScheduler.masterWaitTicksThisFrame > 0u ||
                      simulationScheduler.slaveLastJobTicksThisFrame > 0u ||
                      simulationScheduler.slaveBackoffFrames > 0u ||
                      simulationScheduler.jobInFlight ||
                      simulationScheduler.hasCompleted ||
                      hasTrackProducerState ||
                      trackProducerJobInFlight ||
                      trackProducerSafeModeActive;
    outPacket.simulationScheduler = simulationScheduler;
    outPacket.hasTrackProducerState = hasTrackProducerState;
    outPacket.trackProducerJobInFlight = trackProducerJobInFlight;
    outPacket.trackProducerSafeModeActive = trackProducerSafeModeActive;
    outPacket.reuse = reuse;
}

inline void SeedSchedulerReuseObservabilityPacket(
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& simulationScheduler,
    const GameLoopRuntime::TrackRenderProducerStatePacket& trackProducerState,
    const ReuseObservabilityPacket& reuse,
    SchedulerReuseObservabilityPacket& outPacket)
{
    SeedSchedulerReuseObservabilityPacket(simulationScheduler,
                                          trackProducerState.valid,
                                          trackProducerState.producerJobInFlight,
                                          trackProducerState.producerSafeModeActive,
                                          reuse,
                                          outPacket);
}

inline SchedulerReuseObservabilityPacket BuildSchedulerReuseObservabilityPacket(
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& simulationScheduler,
    bool hasTrackProducerState,
    bool trackProducerJobInFlight,
    bool trackProducerSafeModeActive,
    const ReuseObservabilityPacket& reuse)
{
    SchedulerReuseObservabilityPacket packet{};
    SeedSchedulerReuseObservabilityPacket(simulationScheduler,
                                          hasTrackProducerState,
                                          trackProducerJobInFlight,
                                          trackProducerSafeModeActive,
                                          reuse,
                                          packet);
    return packet;
}

inline SchedulerReuseObservabilityPacket BuildSchedulerReuseObservabilityPacket(
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& simulationScheduler,
    const GameLoopRuntime::TrackRenderProducerStatePacket& trackProducerState,
    const ReuseObservabilityPacket& reuse)
{
    SchedulerReuseObservabilityPacket packet{};
    SeedSchedulerReuseObservabilityPacket(
        simulationScheduler,
        trackProducerState,
        reuse,
        packet);
    return packet;
}

} // namespace GameLoopObservabilityDomain
