#pragma once

#include "game_loop_observability_debug_contracts.hpp"
#include "game_loop_overlay_debug_contracts.hpp"
#include "game_loop_presenter_overlay_debug_contracts.hpp"
#include "game_loop_presenter_input_summary_contracts.hpp"
#include "game_loop_presentation_debug_contracts.hpp"
#include "game_loop_presenter_render_debug_contracts.hpp"
#include "game_loop_render_debug_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterInputBundle
{
    bool valid = false;
    PresentationDebugBundle presentation{};
    RenderFrameDebugBundle render{};
    PresenterRenderDebugPacket renderDebug{};
    GameLoopObservabilityDomain::OverlayDebugBundle overlay{};
    PresenterOverlayDebugPacket overlayDebug{};
    GameLoopObservabilityDomain::ObservabilityDebugBundle observability{};
    PresenterInputSummaryPacket summary{};
};

} // namespace GameLoopRuntime
