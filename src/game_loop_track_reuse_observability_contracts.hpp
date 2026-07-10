#pragma once

#include "game_loop_track_reuse_decision_view_contracts.hpp"
#include "game_loop_track_reuse_telemetry_view_contracts.hpp"

namespace GameLoopRuntime
{

struct TrackReuseObservabilityPacket
{
    bool valid = false;
    TrackReuseDecisionViewPacket decision{};
    TrackReuseTelemetryViewPacket telemetry{};
};

} // namespace GameLoopRuntime
