#pragma once

#include "game_loop_scheduler_reuse_debug_telemetry_contracts.hpp"
#include "game_loop_scheduler_reuse_flow_observability_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedSchedulerReuseDebugTelemetryPacket(
    const GameLoopObservabilityDomain::SchedulerReuseFlowObservabilityPacket& flow,
    SchedulerReuseDebugTelemetryPacket& outPacket)
{
    outPacket.valid = flow.valid;
}

inline SchedulerReuseDebugTelemetryPacket BuildSchedulerReuseDebugTelemetryPacket(
    const GameLoopObservabilityDomain::SchedulerReuseFlowObservabilityPacket& flow)
{
    SchedulerReuseDebugTelemetryPacket packet{};
    SeedSchedulerReuseDebugTelemetryPacket(flow, packet);
    return packet;
}

inline SchedulerReuseDebugTelemetryPacket BuildSchedulerReuseDebugTelemetryPacket(
    const GameLoopObservabilityDomain::SchedulerReuseObservabilityPacket& schedulerReuse)
{
    return SchedulerReuseDebugTelemetryPacket{schedulerReuse.valid};
}

} // namespace GameLoopRuntime
