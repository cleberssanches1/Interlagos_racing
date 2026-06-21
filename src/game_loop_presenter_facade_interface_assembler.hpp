#pragma once

#include "game_loop_presenter_facade_interface_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterFacadeRequestPacket(const PresenterFacadePacket& facade,
                                             PresenterFacadeRequestPacket& outPacket)
{
    outPacket.valid = facade.valid;
    outPacket.facade = facade;
}

inline PresenterFacadeRequestPacket BuildPresenterFacadeRequestPacket(
    const PresenterFacadePacket& facade)
{
    PresenterFacadeRequestPacket packet{};
    SeedPresenterFacadeRequestPacket(facade, packet);
    return packet;
}

inline void SeedPresenterFacadeDecisionPacket(const PresenterFacadePacket& facade,
                                              PresenterFacadeDecisionPacket& outPacket)
{
    outPacket.valid = facade.valid;
    outPacket.phase = PresenterFacadePhase::Summary;
    outPacket.shouldPresentHud = facade.drivingHud.valid;
    outPacket.shouldPresentPeriodicHud = facade.periodicHud.valid;
    outPacket.shouldPresentRenderDebug = facade.render.valid;
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

} // namespace GameLoopRuntime
