#pragma once

#include "game_loop_presentation_ops.hpp"
#include "game_loop_scheduler_reuse_debug_telemetry_contracts.hpp"
#include "game_loop_scheduler_reuse_flow_observability_contracts.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"

namespace GameLoopTelemetryDomain
{

inline void SeedSchedulerReuseDebugTelemetryPacket(
    const GameLoopObservabilityDomain::SchedulerReuseFlowObservabilityPacket& observability,
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& trackTelemetry,
    bool includeQueryTelemetry,
    SchedulerReuseDebugTelemetryPacket& outPacket)
{
    (void)includeQueryTelemetry;
    outPacket.valid = observability.valid || trackTelemetry.valid;
}

inline SchedulerReuseDebugTelemetryPacket BuildSchedulerReuseDebugTelemetryPacket(
    const GameLoopObservabilityDomain::SchedulerReuseFlowObservabilityPacket& observability,
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& trackTelemetry,
    bool includeQueryTelemetry)
{
    SchedulerReuseDebugTelemetryPacket packet{};
    SeedSchedulerReuseDebugTelemetryPacket(
        observability,
        trackTelemetry,
        includeQueryTelemetry,
        packet);
    return packet;
}

} // namespace GameLoopTelemetryDomain
