#pragma once

#include "game_loop_memory_debug_contracts.hpp"
#include "game_loop_observability_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct ObservabilityDebugBundle
{
    bool valid = false;
    FrameObservabilityPacket frame{};
    GameLoopMemoryPresentationDomain::MemoryDebugPresentationBundle memoryDebug{};
};

} // namespace GameLoopObservabilityDomain
