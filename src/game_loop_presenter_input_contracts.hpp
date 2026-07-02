#pragma once

#include "game_loop_presenter_observability_input_contracts.hpp"
#include "game_loop_presentation_debug_contracts.hpp"
#include "game_loop_presenter_render_debug_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterInputBundle
{
    bool valid = false;
    PresentationDebugBundle presentation{};
    PresenterRenderDebugPacket renderDebug{};
    PresenterObservabilityInputPacket observabilityInput{};
};

} // namespace GameLoopRuntime
