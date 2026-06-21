#pragma once

#include "game_loop_presenter_facade_contracts.hpp"
#include "game_loop_presenter_facade_interface_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterFacadeBridgePacket
{
    bool valid = false;
    PresenterFacadePacket facade{};
    PresenterFacadeRequestPacket request{};
    PresenterFacadeDecisionPacket decision{};
};

} // namespace GameLoopRuntime
