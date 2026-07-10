#pragma once

#include "frame_reuse_contracts.hpp"

namespace GameLoopRuntime
{

struct TrackReuseRuntimeState
{
    FrameReuseDomain::TrackFrameHistoryState history{};
    uint32_t requestFrameId = 0u;
    int16_t activeSegmentId = -1;
    bool renderEnabled = false;
    bool producerJobInFlight = false;
};

} // namespace GameLoopRuntime
