#pragma once

namespace GameLoopObservabilityDomain
{

struct ReuseObservabilityDebugPacket
{
    bool valid = false;

    bool simulationShouldConsumeCommitted = false;
    bool simulationShouldDispatchNextFrame = false;
    bool simulationRequiresLockstepWait = false;
    bool simulationRequiresSynchronousFallback = false;

    bool trackShouldConsumeCommitted = false;
    bool trackShouldKickProducer = false;
    bool trackRequiresLockstepWait = false;
    bool trackRequiresSynchronousFallback = false;
};

} // namespace GameLoopObservabilityDomain
