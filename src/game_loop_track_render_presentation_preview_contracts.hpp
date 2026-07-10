#pragma once

#include "game_loop_track_render_producer_hint_contracts.hpp"
#include "game_loop_track_render_sh2_presentation_contracts.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"

namespace GameLoopRuntime
{

struct TrackRenderPresentationPreviewPacket
{
    bool valid = false;
    TrackRenderTelemetryViewPacket telemetryView{};
    TrackRenderProducerHintPacket producerHint{};
    GameLoopObservabilityDomain::TrackRenderSh2PresentationPacket sh2Presentation{};
};

} // namespace GameLoopRuntime
