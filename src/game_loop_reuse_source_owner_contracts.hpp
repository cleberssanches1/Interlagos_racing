#pragma once

#include "game_loop_reuse_source_state_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct ReuseObservabilitySourceOwnerPacket
{
    bool valid = false;
    ReuseObservabilitySourcePacket source{};
};

} // namespace GameLoopObservabilityDomain
