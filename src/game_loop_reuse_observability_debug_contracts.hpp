#pragma once

#include "frame_reuse_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct ReuseObservabilityDebugPacket
{
    bool valid = false;

    bool simulationShouldConsumeCommitted = false;
    bool simulationShouldDispatchNextFrame = false;
    bool simulationRequiresLockstepWait = false;
    bool simulationRequiresSynchronousFallback = false;
    FrameReuseDomain::ReuseMode simulationReuseMode = FrameReuseDomain::ReuseMode::Lockstep;
    uint32_t simulationPacketsCommitted = 0u;
    uint32_t simulationPreviousFrameConsumes = 0u;
    uint32_t simulationLockstepConsumes = 0u;
    uint32_t simulationFallbacks = 0u;

    bool trackShouldConsumeCommitted = false;
    bool trackShouldKickProducer = false;
    bool trackRequiresLockstepWait = false;
    bool trackRequiresSynchronousFallback = false;
    FrameReuseDomain::ReuseMode trackReuseMode = FrameReuseDomain::ReuseMode::Lockstep;
    uint32_t trackPacketsCommitted = 0u;
    uint32_t trackPreviousFrameConsumes = 0u;
    uint32_t trackLockstepConsumes = 0u;
    uint32_t trackFallbacks = 0u;
};

} // namespace GameLoopObservabilityDomain
