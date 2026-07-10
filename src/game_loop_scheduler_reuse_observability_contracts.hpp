#pragma once

#include "game_loop_reuse_observability_contracts.hpp"
#include "game_loop_simulation_scheduler_telemetry_view_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct SchedulerReuseObservabilityPacket
{
    bool valid = false;
    GameLoopRuntime::SimulationSchedulerTelemetryViewPacket schedulerTelemetry{};
    bool hasProducerState = false;
    bool producerJobInFlight = false;
    bool producerSafeModeActive = false;
    ReuseObservabilityPacket reuse{};
};

struct SchedulerReuseObservabilityAssemblyInputs
{
    GameLoopRuntime::SimulationSchedulerTelemetryViewPacket schedulerTelemetry{};
    bool hasProducerState = false;
    bool producerJobInFlight = false;
    bool producerSafeModeActive = false;
    ReuseObservabilityPacket reuse{};
};

} // namespace GameLoopObservabilityDomain
