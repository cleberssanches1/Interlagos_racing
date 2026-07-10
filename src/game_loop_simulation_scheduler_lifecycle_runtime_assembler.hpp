#pragma once

#include "game_loop_simulation_completion_view_assembler.hpp"
#include "game_loop_simulation_drain_view_assembler.hpp"
#include "game_loop_simulation_scheduler_lifecycle_observability_assembler.hpp"
#include "game_loop_simulation_scheduler_telemetry_view_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline SimulationSchedulerLifecycleObservabilityPacket
BuildSimulationSchedulerLifecycleObservabilityPacket(
    const SimulationSchedulerDomain::SimulationDrainPacket& drain,
    const SimulationSchedulerDomain::SimulationCompletionPacket& completion,
    const SimulationSchedulerDomain::SimulationSchedulerTelemetry& telemetry)
{
    return BuildSimulationSchedulerLifecycleObservabilityPacket(
        GameLoopRuntime::BuildSimulationDrainViewPacket(drain),
        GameLoopRuntime::BuildSimulationCompletionViewPacket(completion),
        GameLoopRuntime::BuildSimulationSchedulerTelemetryViewPacket(telemetry));
}

} // namespace GameLoopObservabilityDomain
