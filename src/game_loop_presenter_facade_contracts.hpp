#pragma once

#include "game_loop_presenter_input_summary_contracts.hpp"
#include "game_loop_presenter_overlay_debug_contracts.hpp"
#include "game_loop_presenter_render_debug_contracts.hpp"
#include "game_loop_presentation_debug_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterFacadePacket
{
    bool valid = false;
    PresenterInputSummaryPacket summary{};
    DrivingHudTextPacket drivingHud{};
    PeriodicHudStatsPacket periodicHud{};
    PresenterRenderDebugPacket render{};
    PresenterOverlayDebugPacket overlay{};
};

} // namespace GameLoopRuntime
