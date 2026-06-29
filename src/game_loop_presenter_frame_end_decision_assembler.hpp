#pragma once

#include "game_loop_presenter_facade_interface_contracts.hpp"
#include "game_loop_presenter_frame_end_decision_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterFrameEndDecisionPacket(const PresenterFacadeDecisionPacket& decision,
                                                PresenterFrameEndDecisionPacket& outPacket)
{
    outPacket.valid = decision.valid;
    outPacket.shouldPresentDrivingHud = decision.shouldPresentHud;
    outPacket.shouldPresentPeriodicHud = decision.shouldPresentPeriodicHud;
    outPacket.shouldPresentOverlayDebug = decision.shouldPresentOverlayDebug;
    outPacket.shouldPresentMemoryDebug = decision.shouldPresentMemoryDebug;
}

inline PresenterFrameEndDecisionPacket BuildPresenterFrameEndDecisionPacket(
    const PresenterFacadeDecisionPacket& decision)
{
    PresenterFrameEndDecisionPacket packet{};
    SeedPresenterFrameEndDecisionPacket(decision, packet);
    return packet;
}

} // namespace GameLoopRuntime
