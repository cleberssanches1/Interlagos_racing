#pragma once

#include "frame_reuse_contracts.hpp"
#include "game_loop_track_reuse_telemetry_view_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedTrackReuseTelemetryViewPacket(const FrameReuseDomain::FrameReuseTelemetry& telemetry,
                                              TrackReuseTelemetryViewPacket& outPacket)
{
    outPacket.trackPacketsCommitted = telemetry.trackPacketsCommitted;
    outPacket.trackPreviousFrameConsumes = telemetry.trackPreviousFrameConsumes;
    outPacket.trackLockstepConsumes = telemetry.trackLockstepConsumes;
    outPacket.trackFallbacks = telemetry.trackFallbacks;
}

inline TrackReuseTelemetryViewPacket BuildTrackReuseTelemetryViewPacket(
    const FrameReuseDomain::FrameReuseTelemetry& telemetry)
{
    TrackReuseTelemetryViewPacket packet{};
    SeedTrackReuseTelemetryViewPacket(telemetry, packet);
    return packet;
}

} // namespace GameLoopRuntime
