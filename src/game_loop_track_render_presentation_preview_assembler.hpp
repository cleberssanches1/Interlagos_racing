#pragma once

#include "game_loop_track_render_producer_hint_assembler.hpp"
#include "game_loop_track_render_presentation_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline bool HasTrackRenderProducerHintData(const TrackRenderProducerHintPacket& producerHint)
{
    return producerHint.producerJobInFlight;
}

inline void SeedTrackRenderPresentationPreviewPacket(
    const TrackRenderTelemetryViewPacket& telemetryView,
    const TrackRenderProducerHintPacket& producerHint,
    const GameLoopObservabilityDomain::TrackRenderSh2PresentationPacket& sh2Presentation,
    TrackRenderPresentationPreviewPacket& outPacket)
{
    outPacket.valid = telemetryView.valid || HasTrackRenderProducerHintData(producerHint)
        || sh2Presentation.valid;
    outPacket.telemetryView = telemetryView;
    outPacket.producerHint = producerHint;
    outPacket.sh2Presentation = sh2Presentation;
}

inline TrackRenderPresentationPreviewPacket BuildTrackRenderPresentationPreviewPacket(
    const TrackRenderTelemetryViewPacket& telemetryView,
    const TrackRenderProducerHintPacket& producerHint,
    const GameLoopObservabilityDomain::TrackRenderSh2PresentationPacket& sh2Presentation)
{
    TrackRenderPresentationPreviewPacket packet{};
    SeedTrackRenderPresentationPreviewPacket(
        telemetryView,
        producerHint,
        sh2Presentation,
        packet);
    return packet;
}

inline TrackRenderPresentationPreviewPacket BuildTrackRenderPresentationPreviewPacket(
    const TrackRenderTelemetryViewPacket& telemetryView,
    const GameLoopObservabilityDomain::TrackRenderSh2PresentationPacket& sh2Presentation)
{
    return BuildTrackRenderPresentationPreviewPacket(
        telemetryView,
        BuildTrackRenderProducerHintPacket(telemetryView),
        sh2Presentation);
}

} // namespace GameLoopRuntime
