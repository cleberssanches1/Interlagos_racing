#pragma once

#include "game_loop_track_reuse_decision_view_contracts.hpp"
#include "game_loop_track_reuse_observability_contracts.hpp"
#include "game_loop_track_reuse_telemetry_view_contracts.hpp"

namespace GameLoopRuntime
{

struct TrackReusePreviewPacket
{
    bool valid = false;
    TrackReuseDecisionViewPacket decision{};
    TrackReuseTelemetryViewPacket telemetry{};
    TrackReuseObservabilityPacket observability{};
};

} // namespace GameLoopRuntime
