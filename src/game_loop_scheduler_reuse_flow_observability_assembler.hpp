#pragma once

#include "game_loop_scheduler_reuse_flow_observability_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedSchedulerReuseFlowObservabilityPacket(
    const SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const SchedulerReuseObservabilityPacket& schedulerReuse,
    SchedulerReuseFlowObservabilityPacket& outPacket)
{
    outPacket.valid = lifecycle.valid || schedulerReuse.valid;
    outPacket.lifecycle = lifecycle;
    outPacket.schedulerReuse = schedulerReuse;
}

inline SchedulerReuseFlowObservabilityPacket BuildSchedulerReuseFlowObservabilityPacket(
    const SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const SchedulerReuseObservabilityPacket& schedulerReuse)
{
    SchedulerReuseFlowObservabilityPacket packet{};
    SeedSchedulerReuseFlowObservabilityPacket(lifecycle, schedulerReuse, packet);
    return packet;
}

} // namespace GameLoopObservabilityDomain
