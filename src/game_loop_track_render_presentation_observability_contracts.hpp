#pragma once

#include "game_loop_runtime_state.hpp"

namespace GameLoopObservabilityDomain
{

struct TrackRenderPresentationObservabilityPacket
{
    bool valid = false;
    bool hasProducerState = false;
    bool producerJobInFlight = false;
    bool producerSafeModeActive = false;
    GameLoopRuntime::Sh2SplitTelemetrySnapshot sh2{};
};

} // namespace GameLoopObservabilityDomain
