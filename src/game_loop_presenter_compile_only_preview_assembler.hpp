#pragma once

#include "game_loop_presenter_compile_only_preview_contracts.hpp"
#include "game_loop_presenter_facade_assembler.hpp"
#include "game_loop_presenter_facade_bridge_assembler.hpp"
#include "game_loop_presenter_facade_decision_bridge_assembler.hpp"
#include "game_loop_presenter_facade_decision_input_assembler.hpp"
#include "game_loop_presenter_input_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterCompileOnlyPreviewPacket(
    const PresenterFacadeBridgePacket& facadeBridge,
    const PresenterFacadeDecisionBridgePacket& decisionBridge,
    PresenterCompileOnlyPreviewPacket& outPacket)
{
    outPacket.valid = facadeBridge.valid || decisionBridge.valid;
    outPacket.facadeBridge = facadeBridge;
    outPacket.decisionBridge = decisionBridge;
}

inline PresenterCompileOnlyPreviewPacket BuildPresenterCompileOnlyPreviewPacket(
    const PresenterFacadeBridgePacket& facadeBridge,
    const PresenterFacadeDecisionBridgePacket& decisionBridge)
{
    PresenterCompileOnlyPreviewPacket packet{};
    SeedPresenterCompileOnlyPreviewPacket(facadeBridge, decisionBridge, packet);
    return packet;
}

inline PresenterCompileOnlyPreviewPacket BuildPresenterCompileOnlyPreviewPacket(
    const PresenterInputBundle& inputBundle,
    const PresenterHudTelemetryDecisionInputPacket& hudTelemetryInput)
{
    const PresenterFacadePacket facade = BuildPresenterFacadePacket(inputBundle);
    return BuildPresenterCompileOnlyPreviewPacket(
        BuildPresenterFacadeBridgePacket(facade),
        BuildPresenterFacadeDecisionBridgePacket(
            BuildPresenterFacadeDecisionInputPacket(facade),
            hudTelemetryInput));
}

inline PresenterCompileOnlyPreviewPacket BuildPresenterCompileOnlyPreviewPacket(
    const PresenterFacadeInputPacket& facadeInput,
    const PresenterHudTelemetryDecisionInputPacket& hudTelemetryInput)
{
    const PresenterFacadePacket facade = BuildPresenterFacadePacket(facadeInput);
    return BuildPresenterCompileOnlyPreviewPacket(
        BuildPresenterFacadeBridgePacket(facade),
        BuildPresenterFacadeDecisionBridgePacket(
            BuildPresenterFacadeDecisionInputPacket(facadeInput),
            hudTelemetryInput));
}

} // namespace GameLoopRuntime
