#pragma once

#include "game_loop_presenter_facade_decision_input_assembler.hpp"
#include "game_loop_presenter_facade_interface_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterFacadeDecisionPacket(const PresenterFacadePacket& facade,
                                              PresenterFacadeDecisionPacket& outPacket)
{
    outPacket.valid = facade.valid;
    outPacket.shouldPresentHud = facade.drivingHud.valid;
    outPacket.shouldPresentPeriodicHud = facade.periodicHud.valid;
    outPacket.shouldPresentOverlayDebug = facade.overlay.valid;
    outPacket.shouldPresentMemoryDebug = facade.overlay.hasMemoryDebug;
}

inline PresenterFacadeDecisionPacket BuildPresenterFacadeDecisionPacket(
    const PresenterFacadePacket& facade)
{
    PresenterFacadeDecisionPacket packet{};
    SeedPresenterFacadeDecisionPacket(facade, packet);
    return packet;
}

inline void SeedPresenterFacadeDecisionPacket(
    const PresenterFacadeDecisionInputPacket& decisionInput,
    PresenterFacadeDecisionPacket& outPacket)
{
    outPacket.valid = decisionInput.valid;
    outPacket.shouldPresentHud = decisionInput.hasDrivingHud;
    outPacket.shouldPresentPeriodicHud = decisionInput.hasPeriodicHud;
    outPacket.shouldPresentOverlayDebug = decisionInput.hasOverlayDebug;
    outPacket.shouldPresentMemoryDebug = decisionInput.hasMemoryDebug;
}

inline PresenterFacadeDecisionPacket BuildPresenterFacadeDecisionPacket(
    const PresenterFacadeDecisionInputPacket& decisionInput)
{
    PresenterFacadeDecisionPacket packet{};
    SeedPresenterFacadeDecisionPacket(decisionInput, packet);
    return packet;
}

} // namespace GameLoopRuntime
