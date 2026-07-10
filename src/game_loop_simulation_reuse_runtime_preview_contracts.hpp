#pragma once

#include "game_loop_simulation_reuse_decision_view_contracts.hpp"
#include "game_loop_simulation_reuse_runtime_decision_contracts.hpp"

namespace GameLoopRuntime
{

struct SimulationReuseRuntimePreviewPacket
{
    bool valid = false;
    SimulationReuseRuntimeDecisionInputsPacket inputs{};
    SimulationReuseDecisionViewPacket decisionView{};
};

} // namespace GameLoopRuntime
