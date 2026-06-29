#pragma once

#include "game_loop_presenter_facade_bridge_contracts.hpp"
#include "game_loop_presenter_facade_decision_bridge_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterCompileOnlyPreviewPacket
{
    bool valid = false;
    PresenterFacadeBridgePacket facadeBridge{};
    PresenterFacadeDecisionBridgePacket decisionBridge{};
};

} // namespace GameLoopRuntime
