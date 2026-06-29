#pragma once

#include "game_loop_track_render_presentation_observability_contracts.hpp"
#include "game_loop_track_render_producer_state_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedTrackRenderPresentationObservabilityPacket(
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& telemetry,
    const GameLoopRuntime::TrackRenderProducerStatePacket& producerState,
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2,
    TrackRenderPresentationObservabilityPacket& outPacket)
{
    outPacket.valid = telemetry.valid || producerState.valid || sh2.Valid();
    outPacket.telemetry = telemetry;
    outPacket.producerState = producerState;
    outPacket.sh2 = sh2;
}

inline void SeedTrackRenderPresentationObservabilityPacket(
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& telemetry,
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2,
    TrackRenderPresentationObservabilityPacket& outPacket)
{
    SeedTrackRenderPresentationObservabilityPacket(
        telemetry,
        GameLoopRuntime::BuildTrackRenderProducerStatePacket(telemetry),
        sh2,
        outPacket);
}

inline TrackRenderPresentationObservabilityPacket BuildTrackRenderPresentationObservabilityPacket(
    const GameLoopRuntime::TrackRenderTelemetryViewPacket& telemetry,
    const GameLoopRuntime::TrackRenderProducerStatePacket& producerState,
    const GameLoopRuntime::Sh2SplitTelemetrySnapshot& sh2)
{
    TrackRenderPresentationObservabilityPacket packet{};
    SeedTrackRenderPresentationObservabilityPacket(
        telemetry,
        producerState,
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
