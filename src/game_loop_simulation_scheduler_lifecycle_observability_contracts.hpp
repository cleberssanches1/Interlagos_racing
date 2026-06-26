#pragma once

#include "game_loop_simulation_completion_view_contracts.hpp"
#include "game_loop_simulation_drain_view_contracts.hpp"
#include "game_loop_simulation_scheduler_telemetry_view_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct SimulationSchedulerLifecycleObservabilityPacket
{
    bool valid = false;
    GameLoopRuntime::SimulationDrainViewPacket drain{};
    GameLoopRuntime::SimulationCompletionViewPacket completion{};
    GameLoopRuntime::SimulationSchedulerTelemetryViewPacket telemetry{};
};

} // namespace GameLoopObservabilityDomain
