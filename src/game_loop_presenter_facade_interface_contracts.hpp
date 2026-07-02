#pragma once

#include "game_loop_presenter_facade_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterFacadeDecisionPacket
{
    bool valid = false;
    bool shouldPresentHud = false;
    bool shouldPresentPeriodicHud = false;
    bool shouldPresentOverlayDebug = false;
    bool shouldPresentMemoryDebug = false;
};

} // namespace GameLoopRuntime
