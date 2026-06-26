#pragma once

#include "frame_reuse_contracts.hpp"
#include "game_loop_track_reuse_decision_view_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedTrackReuseDecisionViewPacket(const FrameReuseDomain::TrackReuseDecisionPacket& decision,
                                             TrackReuseDecisionViewPacket& outPacket)
{
    outPacket.valid = decision.valid;
    outPacket.shouldConsumeCommitted = decision.shouldConsumeCommitted;
    outPacket.shouldKickProducer = decision.shouldKickProducer;
    outPacket.requiresLockstepWait = decision.requiresLockstepWait;
    outPacket.requiresSynchronousFallback = decision.requiresSynchronousFallback;
    outPacket.mode = decision.mode;
    outPacket.consumeSlot = decision.consumeSlot;
    outPacket.dispatchSlot = decision.dispatchSlot;
}

inline TrackReuseDecisionViewPacket BuildTrackReuseDecisionViewPacket(
    const FrameReuseDomain::TrackReuseDecisionPacket& decision)
{
    TrackReuseDecisionViewPacket packet{};
    SeedTrackReuseDecisionViewPacket(decision, packet);
    return packet;
}

} // namespace GameLoopRuntime
