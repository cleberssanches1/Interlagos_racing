#pragma once

#include "game_loop_runtime_state.hpp"
#include "game_loop_track_render_producer_state_contracts.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct TrackRenderPresentationObservabilityPacket
{
    bool valid = false;
    GameLoopRuntime::TrackRenderTelemetryViewPacket telemetry{};
    GameLoopRuntime::TrackRenderProducerStatePacket producerState{};
    GameLoopRuntime::Sh2SplitTelemetrySnapshot sh2{};
};

} // namespace GameLoopObservabilityDomain
