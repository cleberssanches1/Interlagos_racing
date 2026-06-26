#pragma once

#include <cstdint>

#include "frame_reuse_contracts.hpp"
#include "game_loop_runtime_state.hpp"

namespace GameLoopTelemetryDomain
{

struct SchedulerReuseDebugTelemetryPacket
{
    bool valid = false;
    GameLoopRuntime::Sh2SplitTelemetrySnapshot sh2{};
    bool producerJobInFlight = false;
    bool producerSafeModeActive = false;
    bool simulationJobInFlight = false;
    bool simulationHasCompleted = false;
    bool simulationShouldDispatchNextFrame = false;
    bool simulationRequiresLockstepWait = false;
    bool simulationRequiresSynchronousFallback = false;
    bool trackShouldKickProducer = false;
    bool trackRequiresLockstepWait = false;
    bool trackRequiresSynchronousFallback = false;
    FrameReuseDomain::ReuseMode simulationReuseMode = FrameReuseDomain::ReuseMode::Lockstep;
    FrameReuseDomain::ReuseMode trackReuseMode = FrameReuseDomain::ReuseMode::Lockstep;
    uint32_t simulationFallbacks = 0u;
    uint32_t trackFallbacks = 0u;
};

} // namespace GameLoopTelemetryDomain
