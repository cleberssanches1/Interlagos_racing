#pragma once

#include <cstdint>

#include "game_loop_runtime_state.hpp"

namespace GameLoopObservabilityDomain
{

struct TrackRenderSh2PresentationPacket
{
    bool valid = false;
    bool useSafeTelemetryFormat = false;
    uint32_t slaveDispatchCount = 0u;
    uint32_t slaveDispatchSkipsTrackBusy = 0u;
    GameLoopRuntime::Sh2SplitTelemetrySnapshot sh2{};
    bool hasProducerState = false;
    bool producerJobInFlight = false;
    bool producerSafeModeActive = false;
};

} // namespace GameLoopObservabilityDomain
