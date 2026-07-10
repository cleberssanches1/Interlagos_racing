#pragma once

#include "game_loop_scheduler_reuse_debug_telemetry_contracts.hpp"
#include "game_loop_scheduler_reuse_flow_observability_contracts.hpp"

namespace GameLoopRuntime
{

struct SchedulerReusePreviewPacket
{
    bool valid = false;
    GameLoopObservabilityDomain::SchedulerReuseFlowObservabilityPacket flow{};
    SchedulerReuseDebugTelemetryPacket debugTelemetry{};
};

} // namespace GameLoopRuntime
