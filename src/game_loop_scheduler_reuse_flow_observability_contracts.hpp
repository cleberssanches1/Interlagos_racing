#pragma once

#include "game_loop_scheduler_reuse_observability_contracts.hpp"
#include "game_loop_simulation_scheduler_lifecycle_observability_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct SchedulerReuseFlowObservabilityPacket
{
    bool valid = false;
    SimulationSchedulerLifecycleObservabilityPacket lifecycle{};
    SchedulerReuseObservabilityPacket schedulerReuse{};
};

} // namespace GameLoopObservabilityDomain
