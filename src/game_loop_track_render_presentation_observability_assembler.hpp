#pragma once

#include "game_loop_track_render_presentation_observability_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedTrackRenderPresentationObservabilityPacket(
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& telemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2,
    TrackRenderPresentationObservabilityPacket& outPacket)
{
    outPacket.valid = telemetry.valid || hasProducerState || sh2.Valid();
    outPacket.hasProducerState = hasProducerState;
    outPacket.producerJobInFlight = producerJobInFlight;
    outPacket.producerSafeModeActive = producerSafeModeActive;
    outPacket.sh2 = sh2;
}

inline void SeedTrackRenderPresentationObservabilityPacket(
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& telemetry,
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2,
    TrackRenderPresentationObservabilityPacket& outPacket)
{
    SeedTrackRenderPresentationObservabilityPacket(
        telemetry,
        telemetry.valid,
        telemetry.producerJobInFlight,
        telemetry.producerSafeModeActive,
        sh2,
        outPacket);
}

inline TrackRenderPresentationObservabilityPacket BuildTrackRenderPresentationObservabilityPacket(
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& telemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2)
{
    TrackRenderPresentationObservabilityPacket packet{};
    SeedTrackRenderPresentationObservabilityPacket(telemetry,
                                                   hasProducerState,
                                                   producerJobInFlight,
                                                   producerSafeModeActive,
                                                   sh2,
                                                   packet);
    return packet;
}

inline TrackRenderPresentationObservabilityPacket BuildTrackRenderPresentationObservabilityPacket(
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& telemetry,
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2)
{
    TrackRenderPresentationObservabilityPacket packet{};
    SeedTrackRenderPresentationObservabilityPacket(telemetry, sh2, packet);
    return packet;
}

} // namespace GameLoopObservabilityDomain
