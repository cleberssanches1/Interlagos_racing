#pragma once

#include "game_loop_presenter_facade_assembler.hpp"
#include "game_loop_presenter_facade_bridge_contracts.hpp"
#include "game_loop_presenter_facade_interface_assembler.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterFacadeBridgePacket(const PresenterFacadePacket& facade,
                                            PresenterFacadeBridgePacket& outPacket)
{
    outPacket.valid = facade.valid;
    outPacket.facade = facade;
    outPacket.request = BuildPresenterFacadeRequestPacket(facade);
    outPacket.decision = BuildPresenterFacadeDecisionPacket(outPacket.request.facade);
}

inline PresenterFacadeBridgePacket BuildPresenterFacadeBridgePacket(
    const PresenterFacadePacket& facade)
{
    PresenterFacadeBridgePacket packet{};
    SeedPresenterFacadeBridgePacket(facade, packet);
    return packet;
}

} // namespace GameLoopRuntime
