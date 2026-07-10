#pragma once

#include "game_loop_simulation_reuse_runtime_preview_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct SimulationReuseRuntimeDebugPreviewPacket
{
    bool valid = false;
    GameLoopRuntime::SimulationReuseRuntimePreviewPacket preview{};
};

} // namespace GameLoopObservabilityDomain
