#pragma once

#include "game_loop_presenter_facade_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterFacadeBridgePacket
{
    bool valid = false;
    PresenterFacadePacket facade{};
};

} // namespace GameLoopRuntime
