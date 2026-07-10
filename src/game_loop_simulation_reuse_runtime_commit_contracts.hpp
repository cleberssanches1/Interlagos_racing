#pragma once

#include "simulation_scheduler_state.hpp"

namespace GameLoopRuntime
{

struct SimulationReuseRuntimeCommitPacket
{
    bool valid = false;
    Game::SimulationPayload authoritativeOutput{};
};

} // namespace GameLoopRuntime
