#pragma once

#include "game_loop_presenter_facade_decision_input_contracts.hpp"
#include "game_loop_presenter_frame_end_decision_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterFrameEndPreviewPacket
{
    bool valid = false;
    PresenterFacadeDecisionInputPacket decisionInput{};
    PresenterFrameEndDecisionPacket decision{};
};

} // namespace GameLoopRuntime
