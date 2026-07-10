#pragma once

#include "game_loop_reuse_observability_contracts.hpp"
#include "game_loop_track_reuse_observability_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedReuseObservabilityPacket(
    const GameLoopRuntime::SimulationReuseDecisionViewPacket& simulationDecision,
    const GameLoopRuntime::SimulationReuseTelemetryViewPacket& simulationTelemetry,
    const GameLoopRuntime::TrackReuseDecisionViewPacket& trackDecision,
    const GameLoopRuntime::TrackReuseTelemetryViewPacket& trackTelemetry,
    ReuseObservabilityPacket& outPacket)
{
    outPacket.valid = simulationDecision.valid ||
                      simulationTelemetry.simulationPacketsCommitted > 0u ||
                      simulationTelemetry.simulationPreviousFrameConsumes > 0u ||
                      simulationTelemetry.simulationLockstepConsumes > 0u ||
                      simulationTelemetry.simulationFallbacks > 0u ||
                      trackDecision.valid ||
                      trackTelemetry.trackPacketsCommitted > 0u ||
                      trackTelemetry.trackPreviousFrameConsumes > 0u ||
                      trackTelemetry.trackLockstepConsumes > 0u ||
                      trackTelemetry.trackFallbacks > 0u;
    outPacket.simulationDecision = simulationDecision;
    outPacket.simulationTelemetry = simulationTelemetry;
    outPacket.track =
        GameLoopRuntime::BuildTrackReuseObservabilityPacket(trackDecision, trackTelemetry);
    outPacket.trackDecision = trackDecision;
    outPacket.trackTelemetry = trackTelemetry;
}

inline ReuseObservabilityPacket BuildReuseObservabilityPacket(
    const GameLoopRuntime::SimulationReuseDecisionViewPacket& simulationDecision,
    const GameLoopRuntime::SimulationReuseTelemetryViewPacket& simulationTelemetry,
    const GameLoopRuntime::TrackReuseDecisionViewPacket& trackDecision,
    const GameLoopRuntime::TrackReuseTelemetryViewPacket& trackTelemetry)
{
    ReuseObservabilityPacket packet{};
    SeedReuseObservabilityPacket(
        simulationDecision,
        simulationTelemetry,
        trackDecision,
        trackTelemetry,
        packet);
    return packet;
}

} // namespace GameLoopObservabilityDomain
