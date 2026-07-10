#pragma once

#include "game_loop_scheduler_reuse_debug_telemetry_assembler.hpp"
#include "game_loop_scheduler_reuse_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedSchedulerReusePreviewPacket(
    const GameLoopObservabilityDomain::SchedulerReuseFlowObservabilityPacket& flow,
    const SchedulerReuseDebugTelemetryPacket& debugTelemetry,
    SchedulerReusePreviewPacket& outPacket)
{
    outPacket.valid = flow.valid || debugTelemetry.valid;
    outPacket.flow = flow;
    outPacket.debugTelemetry = debugTelemetry;
}

inline SchedulerReusePreviewPacket BuildSchedulerReusePreviewPacket(
    const GameLoopObservabilityDomain::SchedulerReuseFlowObservabilityPacket& flow,
    const SchedulerReuseDebugTelemetryPacket& debugTelemetry)
{
    SchedulerReusePreviewPacket packet{};
    SeedSchedulerReusePreviewPacket(flow, debugTelemetry, packet);
    return packet;
}

inline SchedulerReusePreviewPacket BuildSchedulerReusePreviewPacket(
    const GameLoopObservabilityDomain::SchedulerReuseFlowObservabilityPacket& flow)
{
    return BuildSchedulerReusePreviewPacket(
        flow,
        BuildSchedulerReuseDebugTelemetryPacket(flow));
}

} // namespace GameLoopRuntime
