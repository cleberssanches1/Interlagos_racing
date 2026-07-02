#pragma once

#include "game_loop_presenter_overlay_debug_contracts.hpp"
#include "game_loop_presentation_debug_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterFacadeInputPacket
{
    bool valid = false;
    DrivingHudTextPacket drivingHud{};
    PeriodicHudStatsPacket periodicHud{};
    PresenterOverlayDebugPacket overlay{};
};

} // namespace GameLoopRuntime
