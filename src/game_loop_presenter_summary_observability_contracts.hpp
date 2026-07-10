#pragma once

#include "game_loop_presenter_input_summary_contracts.hpp"
#include "game_loop_presenter_observability_input_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterSummaryObservabilityPacket
{
    bool valid = false;
    PresenterInputSummaryPacket summary{};
    PresenterObservabilityInputPacket observabilityInput{};
};

} // namespace GameLoopRuntime
