#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct TrackReuseTelemetryViewPacket
{
    uint32_t trackPacketsCommitted = 0u;
    uint32_t trackPreviousFrameConsumes = 0u;
    uint32_t trackLockstepConsumes = 0u;
    uint32_t trackFallbacks = 0u;
};

} // namespace GameLoopRuntime
