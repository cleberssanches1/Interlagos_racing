#pragma once

#include "game_loop_presenter_facade_assembler.hpp"
#include "game_loop_presenter_facade_bridge_contracts.hpp"
#include "game_loop_presenter_facade_decision_input_assembler.hpp"
#include "game_loop_presenter_facade_input_assembler.hpp"
#include "game_loop_presenter_facade_interface_assembler.hpp"
#include "game_loop_presenter_input_contracts.hpp"

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

inline PresenterFacadeBridgePacket BuildPresenterFacadeBridgePacket(
    const PresenterInputBundle& inputBundle)
{
    return BuildPresenterFacadeBridgePacket(BuildPresenterFacadePacket(inputBundle));
}

inline PresenterFacadeBridgePacket BuildPresenterFacadeBridgePacket(
    const PresenterFacadeInputPacket& facadeInput)
{
    return BuildPresenterFacadeBridgePacket(BuildPresenterFacadePacket(facadeInput));
}

inline PresenterFacadeBridgePacket BuildPresenterFacadeBridgePacket(
    const PresenterFacadeDecisionInputPacket& decisionInput)
{
    PresenterFacadeBridgePacket packet{};
    packet.valid = decisionInput.valid;
    packet.decision = BuildPresenterFacadeDecisionPacket(decisionInput);
    packet.request.valid = decisionInput.valid;
    packet.facade.valid = decisionInput.valid;
    return packet;
}

} // namespace GameLoopRuntime
