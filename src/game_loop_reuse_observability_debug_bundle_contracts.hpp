#pragma once

#include "game_loop_reuse_observability_contracts.hpp"
#include "game_loop_reuse_observability_debug_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct ReuseObservabilityDebugBundle
{
    bool valid = false;
    ReuseObservabilityPacket reuse{};
    ReuseObservabilityDebugPacket debug{};
};

} // namespace GameLoopObservabilityDomain
