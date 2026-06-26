#pragma once

#include "frame_reuse_contracts.hpp"
#include "game_loop_simulation_reuse_decision_view_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedSimulationReuseDecisionViewPacket(
    const FrameReuseDomain::SimulationReuseDecisionPacket& decision,
    SimulationReuseDecisionViewPacket& outPacket)
{
    outPacket.valid = decision.valid;
    outPacket.shouldConsumeCommitted = decision.shouldConsumeCommitted;
    outPacket.shouldDispatchNextFrame = decision.shouldDispatchNextFrame;
    outPacket.requiresLockstepWait = decision.requiresLockstepWait;
    outPacket.requiresSynchronousFallback = decision.requiresSynchronousFallback;
    outPacket.mode = decision.mode;
    outPacket.consumeSlot = decision.consumeSlot;
    outPacket.dispatchSlot = decision.dispatchSlot;
}

inline SimulationReuseDecisionViewPacket BuildSimulationReuseDecisionViewPacket(
    const FrameReuseDomain::SimulationReuseDecisionPacket& decision)
{
    SimulationReuseDecisionViewPacket packet{};
    SeedSimulationReuseDecisionViewPacket(decision, packet);
    return packet;
}

} // namespace GameLoopRuntime
