#pragma once

#include "game_loop_presenter_facade_contracts.hpp"
#include "game_loop_presenter_facade_decision_input_contracts.hpp"
#include "game_loop_presenter_facade_input_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterFacadeDecisionInputPacket(const PresenterFacadePacket& facade,
                                                   PresenterFacadeDecisionInputPacket& outPacket)
{
    outPacket.valid = facade.valid;
    outPacket.hasDrivingHud = facade.drivingHud.valid;
    outPacket.hasPeriodicHud = facade.periodicHud.valid;
    outPacket.hasRenderDebug = facade.render.valid;
    outPacket.hasOverlayDebug = facade.overlay.valid;
    outPacket.hasMemoryDebug = facade.overlay.hasMemoryDebug;
}

inline PresenterFacadeDecisionInputPacket BuildPresenterFacadeDecisionInputPacket(
    const PresenterFacadePacket& facade)
{
    PresenterFacadeDecisionInputPacket packet{};
    SeedPresenterFacadeDecisionInputPacket(facade, packet);
    return packet;
}

inline void SeedPresenterFacadeDecisionInputPacket(
    const PresenterFacadeInputPacket& facadeInput,
    PresenterFacadeDecisionInputPacket& outPacket)
{
    outPacket.valid = facadeInput.valid;
    outPacket.hasDrivingHud = facadeInput.drivingHud.valid;
    outPacket.hasPeriodicHud = facadeInput.periodicHud.valid;
    outPacket.hasRenderDebug = facadeInput.render.valid;
    outPacket.hasOverlayDebug = facadeInput.observability.overlayDebug.valid;
    outPacket.hasMemoryDebug = facadeInput.observability.overlayDebug.hasMemoryDebug;
}

inline PresenterFacadeDecisionInputPacket BuildPresenterFacadeDecisionInputPacket(
    const PresenterFacadeInputPacket& facadeInput)
{
    PresenterFacadeDecisionInputPacket packet{};
    SeedPresenterFacadeDecisionInputPacket(facadeInput, packet);
    return packet;
}

} // namespace GameLoopRuntime
