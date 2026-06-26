#pragma once

#include "frame_reuse_contracts.hpp"

namespace GameLoopRuntime
{

struct TrackReuseDecisionViewPacket
{
    bool valid = false;
    bool shouldConsumeCommitted = false;
    bool shouldKickProducer = false;
    bool requiresLockstepWait = false;
    bool requiresSynchronousFallback = false;
    FrameReuseDomain::ReuseMode mode = FrameReuseDomain::ReuseMode::Lockstep;
    uint8_t consumeSlot = 0u;
    uint8_t dispatchSlot = 0u;
};

} // namespace GameLoopRuntime
