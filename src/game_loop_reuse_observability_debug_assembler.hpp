#pragma once

#include "game_loop_reuse_observability_contracts.hpp"
#include "game_loop_reuse_observability_debug_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedReuseObservabilityDebugPacket(const ReuseObservabilityPacket& packet,
                                              ReuseObservabilityDebugPacket& outPacket)
{
    outPacket.valid = packet.valid;

    outPacket.simulationShouldConsumeCommitted = packet.simulationDecision.shouldConsumeCommitted;
    outPacket.simulationShouldDispatchNextFrame = packet.simulationDecision.shouldDispatchNextFrame;
    outPacket.simulationRequiresLockstepWait = packet.simulationDecision.requiresLockstepWait;
    outPacket.simulationRequiresSynchronousFallback =
        packet.simulationDecision.requiresSynchronousFallback;
    outPacket.simulationReuseMode = packet.simulationDecision.mode;
    outPacket.simulationPacketsCommitted = packet.simulationTelemetry.simulationPacketsCommitted;
    outPacket.simulationPreviousFrameConsumes =
        packet.simulationTelemetry.simulationPreviousFrameConsumes;
    outPacket.simulationLockstepConsumes = packet.simulationTelemetry.simulationLockstepConsumes;
    outPacket.simulationFallbacks = packet.simulationTelemetry.simulationFallbacks;

    outPacket.trackShouldConsumeCommitted = packet.trackDecision.shouldConsumeCommitted;
    outPacket.trackShouldKickProducer = packet.trackDecision.shouldKickProducer;
    outPacket.trackRequiresLockstepWait = packet.trackDecision.requiresLockstepWait;
    outPacket.trackRequiresSynchronousFallback =
        packet.trackDecision.requiresSynchronousFallback;
    outPacket.trackReuseMode = packet.trackDecision.mode;
    outPacket.trackPacketsCommitted = packet.trackTelemetry.trackPacketsCommitted;
    outPacket.trackPreviousFrameConsumes = packet.trackTelemetry.trackPreviousFrameConsumes;
    outPacket.trackLockstepConsumes = packet.trackTelemetry.trackLockstepConsumes;
    outPacket.trackFallbacks = packet.trackTelemetry.trackFallbacks;
}

inline ReuseObservabilityDebugPacket BuildReuseObservabilityDebugPacket(
    const ReuseObservabilityPacket& packet)
{
    ReuseObservabilityDebugPacket outPacket{};
    SeedReuseObservabilityDebugPacket(packet, outPacket);
    return outPacket;
}

} // namespace GameLoopObservabilityDomain
