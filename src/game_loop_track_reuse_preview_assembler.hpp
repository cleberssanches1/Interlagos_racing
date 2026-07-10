#pragma once

#include "game_loop_track_reuse_observability_assembler.hpp"
#include "game_loop_track_reuse_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline bool HasTrackReuseTelemetryData(const TrackReuseTelemetryViewPacket& telemetry)
{
    return telemetry.trackPacketsCommitted > 0u ||
           telemetry.trackPreviousFrameConsumes > 0u ||
           telemetry.trackLockstepConsumes > 0u ||
           telemetry.trackFallbacks > 0u;
}

inline void SeedTrackReusePreviewPacket(
    const TrackReuseDecisionViewPacket& decision,
    const TrackReuseTelemetryViewPacket& telemetry,
    const TrackReuseObservabilityPacket& observability,
    TrackReusePreviewPacket& outPacket)
{
    outPacket.valid = decision.valid || HasTrackReuseTelemetryData(telemetry) || observability.valid;
    outPacket.decision = decision;
    outPacket.telemetry = telemetry;
    outPacket.observability = observability;
}

inline TrackReusePreviewPacket BuildTrackReusePreviewPacket(
    const TrackReuseDecisionViewPacket& decision,
    const TrackReuseTelemetryViewPacket& telemetry,
    const TrackReuseObservabilityPacket& observability)
{
    TrackReusePreviewPacket packet{};
    SeedTrackReusePreviewPacket(decision, telemetry, observability, packet);
    return packet;
}

inline TrackReusePreviewPacket BuildTrackReusePreviewPacket(
    const TrackReuseDecisionViewPacket& decision,
    const TrackReuseTelemetryViewPacket& telemetry)
{
    return BuildTrackReusePreviewPacket(
        decision,
        telemetry,
        BuildTrackReuseObservabilityPacket(decision, telemetry));
}

} // namespace GameLoopRuntime
