#pragma once

#include "game_loop_presentation_ops.hpp"
#include "game_loop_track_render_sh2_presentation_assembler.hpp"

namespace GameLoopRuntime
{

inline bool TryBuildTrackRenderSh2PresentationFromTelemetry(
    const Sh2SplitTelemetrySnapshot& sh2Snapshot,
    const TrackRenderTelemetryViewPacket& trackTelemetryView,
    bool useSafeTelemetryFormat,
    uint32_t slaveDispatchCount,
    uint32_t slaveDispatchSkipsTrackBusy,
    GameLoopObservabilityDomain::TrackRenderSh2PresentationPacket& outPacket)
{
    if (!trackTelemetryView.valid)
    {
        outPacket = {};
        return false;
    }

    outPacket = GameLoopObservabilityDomain::BuildTrackRenderSh2PresentationPacket(
        sh2Snapshot,
        useSafeTelemetryFormat,
        slaveDispatchCount,
        slaveDispatchSkipsTrackBusy,
        true,
        trackTelemetryView.producerJobInFlight,
        trackTelemetryView.producerSafeModeActive);
    return outPacket.valid;
}

inline bool TryBuildTrackRenderSh2PresentationFromLifecycle(
    const GameLoopObservabilityDomain::SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const TrackRenderTelemetryViewPacket& trackTelemetryView,
    bool includeQueryTelemetry,
    bool useSafeTelemetryFormat,
    uint32_t slaveDispatchCount,
    uint32_t slaveDispatchSkipsTrackBusy,
    GameLoopObservabilityDomain::TrackRenderSh2PresentationPacket& outPacket)
{
    return TryBuildTrackRenderSh2PresentationFromTelemetry(
        BuildSh2SplitTelemetrySnapshot(lifecycle, trackTelemetryView, includeQueryTelemetry),
        trackTelemetryView,
        useSafeTelemetryFormat,
        slaveDispatchCount,
        slaveDispatchSkipsTrackBusy,
        outPacket);
}

} // namespace GameLoopRuntime
