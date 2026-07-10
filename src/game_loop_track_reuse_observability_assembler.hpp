#pragma once

#include "game_loop_track_reuse_observability_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedTrackReuseObservabilityPacket(
    const TrackReuseDecisionViewPacket& decision,
    const TrackReuseTelemetryViewPacket& telemetry,
    TrackReuseObservabilityPacket& outPacket)
{
    outPacket.valid = decision.valid ||
                      telemetry.trackPacketsCommitted > 0u ||
                      telemetry.trackPreviousFrameConsumes > 0u ||
                      telemetry.trackLockstepConsumes > 0u ||
                      telemetry.trackFallbacks > 0u;
    outPacket.decision = decision;
    outPacket.telemetry = telemetry;
}

inline TrackReuseObservabilityPacket BuildTrackReuseObservabilityPacket(
    const TrackReuseDecisionViewPacket& decision,
    const TrackReuseTelemetryViewPacket& telemetry)
{
    TrackReuseObservabilityPacket packet{};
    SeedTrackReuseObservabilityPacket(decision, telemetry, packet);
    return packet;
}

} // namespace GameLoopRuntime
