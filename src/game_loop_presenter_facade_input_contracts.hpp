#pragma once

#include "game_loop_presenter_input_summary_contracts.hpp"
#include "game_loop_presenter_observability_input_contracts.hpp"
#include "game_loop_presenter_render_debug_contracts.hpp"
#include "game_loop_presentation_debug_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterFacadeInputPacket
{
    bool valid = false;
    PresenterInputSummaryPacket summary{};
    DrivingHudTextPacket drivingHud{};
    PeriodicHudStatsPacket periodicHud{};
    PresenterRenderDebugPacket render{};
    PresenterObservabilityInputPacket observability{};
};

} // namespace GameLoopRuntime
