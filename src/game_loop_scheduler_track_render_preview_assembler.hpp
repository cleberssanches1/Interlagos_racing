#pragma once

#include "game_loop_scheduler_track_render_preview_contracts.hpp"
#include "game_loop_track_render_sh2_presentation_runtime_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedSchedulerTrackRenderPreviewPacket(
    const SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& trackTelemetryView,
    const TrackRenderSh2PresentationPacket& sh2Presentation,
    SchedulerTrackRenderPreviewPacket& outPacket)
{
    outPacket.valid = lifecycle.valid || trackTelemetryView.valid || sh2Presentation.valid;
    outPacket.lifecycle = lifecycle;
    outPacket.trackTelemetryView = trackTelemetryView;
    outPacket.sh2Presentation = sh2Presentation;
}

inline SchedulerTrackRenderPreviewPacket BuildSchedulerTrackRenderPreviewPacket(
    const SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& trackTelemetryView,
    const TrackRenderSh2PresentationPacket& sh2Presentation)
{
    SchedulerTrackRenderPreviewPacket packet{};
    SeedSchedulerTrackRenderPreviewPacket(lifecycle, trackTelemetryView, sh2Presentation, packet);
    return packet;
}

inline SchedulerTrackRenderPreviewPacket BuildSchedulerTrackRenderPreviewPacket(
    const SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& trackTelemetryView,
    bool includeQueryTelemetry,
    bool useSafeTelemetryFormat,
    uint32_t slaveDispatchCount,
    uint32_t slaveDispatchSkipsTrackBusy)
{
    TrackRenderSh2PresentationPacket sh2Presentation{};
    (void)GameLoopRuntime::TryBuildTrackRenderSh2PresentationFromLifecycle(
        lifecycle,
        trackTelemetryView,
        includeQueryTelemetry,
        useSafeTelemetryFormat,
        slaveDispatchCount,
        slaveDispatchSkipsTrackBusy,
        sh2Presentation);
    return BuildSchedulerTrackRenderPreviewPacket(
        lifecycle,
        trackTelemetryView,
        sh2Presentation);
}

} // namespace GameLoopObservabilityDomain
