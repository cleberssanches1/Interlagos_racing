#pragma once

#include "game_loop_presenter_presence_decision_contracts.hpp"
#include "game_loop_presenter_summary_observability_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterPresencePreviewPacket
{
    bool valid = false;
    PresenterSummaryObservabilityPacket presenter{};
    PresenterPresenceDecisionPacket decision{};
};

} // namespace GameLoopRuntime
