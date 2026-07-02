#pragma once

#include "game_loop_reuse_observability_contracts.hpp"
#include "game_loop_simulation_scheduler_telemetry_view_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct SchedulerReuseObservabilityPacket
{
    bool valid = false;
    GameLoopRuntime::SimulationSchedulerTelemetryViewPacket simulationScheduler{};
    bool hasTrackProducerState = false;
    bool trackProducerJobInFlight = false;
    bool trackProducerSafeModeActive = false;
    ReuseObservabilityPacket reuse{};
};

} // namespace GameLoopObservabilityDomain
