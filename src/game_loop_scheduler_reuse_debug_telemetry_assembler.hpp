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
    outPacket.valid = observability.valid || trackTelemetry.valid;
    outPacket.sh2 = GameLoopRuntime::BuildSh2SplitTelemetrySnapshot(
        observability,
        trackTelemetry,
        includeQueryTelemetry);

    outPacket.producerJobInFlight = observability.schedulerReuse.trackProducerState.producerJobInFlight;
    outPacket.producerSafeModeActive = observability.schedulerReuse.trackProducerState.producerSafeModeActive;
    outPacket.simulationJobInFlight = observability.lifecycle.completion.jobInFlight;
    outPacket.simulationHasCompleted = observability.lifecycle.completion.hasCompleted;

    outPacket.simulationShouldDispatchNextFrame =
        observability.schedulerReuse.reuse.simulationDecision.shouldDispatchNextFrame;
    outPacket.simulationRequiresLockstepWait =
        observability.schedulerReuse.reuse.simulationDecision.requiresLockstepWait;
    outPacket.simulationRequiresSynchronousFallback =
        observability.schedulerReuse.reuse.simulationDecision.requiresSynchronousFallback;
    outPacket.trackShouldKickProducer =
        observability.schedulerReuse.reuse.trackDecision.shouldKickProducer;
    outPacket.trackRequiresLockstepWait =
        observability.schedulerReuse.reuse.trackDecision.requiresLockstepWait;
    outPacket.trackRequiresSynchronousFallback =
        observability.schedulerReuse.reuse.trackDecision.requiresSynchronousFallback;

    outPacket.simulationReuseMode = observability.schedulerReuse.reuse.simulationDecision.mode;
    outPacket.trackReuseMode = observability.schedulerReuse.reuse.trackDecision.mode;
    outPacket.simulationFallbacks = observability.schedulerReuse.reuse.simulationTelemetry.simulationFallbacks;
    outPacket.trackFallbacks = observability.schedulerReuse.reuse.trackTelemetry.trackFallbacks;
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
