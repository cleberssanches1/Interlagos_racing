#pragma once

#include "game_loop_presenter_overlay_debug_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterObservabilityInputPacket
{
    bool valid = false;
    PresenterOverlayDebugPacket overlayDebug{};
    bool hasObservability = false;
    bool hasSchedulerReuseDebug = false;
};

} // namespace GameLoopRuntime
