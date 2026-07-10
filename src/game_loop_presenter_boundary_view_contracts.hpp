#pragma once

#include <cstdint>

#include "game_loop_presenter_presence_decision_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterBoundaryViewPacket
{
    bool valid = false;
    PresenterPresenceDecisionPacket presenceDecision{};
    bool hasSchedulerReuseDebug = false;
    bool hasObservability = false;
    int16_t speedKmh = 0;
    char gearChar = 'N';
    int16_t engineRpm = 0;
    uint32_t frameId = 0u;
};

} // namespace GameLoopRuntime
