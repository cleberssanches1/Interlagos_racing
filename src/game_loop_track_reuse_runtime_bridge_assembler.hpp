#pragma once

#include "frame_reuse_runtime_owner_contracts.hpp"
#include "game_loop_track_reuse_decision_view_assembler.hpp"
#include "game_loop_track_reuse_observability_assembler.hpp"
#include "game_loop_track_reuse_preview_assembler.hpp"
#include "game_loop_track_reuse_telemetry_view_assembler.hpp"

namespace GameLoopRuntime
{

inline TrackReuseDecisionViewPacket BuildTrackReuseRuntimeDecisionViewPacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return runtimeOwnerPacket.hasTrackDecision
        ? BuildTrackReuseDecisionViewPacket(runtimeOwnerPacket.trackDecision)
        : TrackReuseDecisionViewPacket{};
}

inline TrackReuseTelemetryViewPacket BuildTrackReuseRuntimeTelemetryViewPacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return runtimeOwnerPacket.hasTelemetry
        ? BuildTrackReuseTelemetryViewPacket(runtimeOwnerPacket.telemetry)
        : TrackReuseTelemetryViewPacket{};
}

inline TrackReuseObservabilityPacket BuildTrackReuseRuntimeObservabilityPacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildTrackReuseObservabilityPacket(
        BuildTrackReuseRuntimeDecisionViewPacket(runtimeOwnerPacket),
        BuildTrackReuseRuntimeTelemetryViewPacket(runtimeOwnerPacket));
}

inline TrackReusePreviewPacket BuildTrackReuseRuntimePreviewPacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildTrackReusePreviewPacket(
        BuildTrackReuseRuntimeDecisionViewPacket(runtimeOwnerPacket),
        BuildTrackReuseRuntimeTelemetryViewPacket(runtimeOwnerPacket));
}

} // namespace GameLoopRuntime
