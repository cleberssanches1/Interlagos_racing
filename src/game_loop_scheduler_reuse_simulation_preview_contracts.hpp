#pragma once

#include "game_loop_scheduler_reuse_flow_observability_contracts.hpp"
#include "game_loop_simulation_reuse_runtime_debug_preview_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct SchedulerReuseSimulationPreviewPacket
{
    bool valid = false;
    SchedulerReuseFlowObservabilityPacket flow{};
    SimulationReuseRuntimeDebugPreviewPacket simulationDebugPreview{};
};

} // namespace GameLoopObservabilityDomain
