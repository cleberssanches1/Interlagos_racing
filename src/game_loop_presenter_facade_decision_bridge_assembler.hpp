#pragma once

#include "game_loop_presenter_facade_decision_bridge_contracts.hpp"
#include "game_loop_presenter_facade_decision_input_assembler.hpp"
#include "game_loop_presenter_facade_interface_assembler.hpp"
#include "game_loop_presenter_frame_end_decision_assembler.hpp"
#include "game_loop_presenter_hud_telemetry_decision_assembler.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterFacadeDecisionBridgePacket(
    const PresenterFacadeDecisionInputPacket& decisionInput,
    const PresenterHudTelemetryDecisionInputPacket& hudTelemetryInput,
    PresenterFacadeDecisionBridgePacket& outPacket)
{
    outPacket.valid = decisionInput.valid || hudTelemetryInput.runtimeStatsEnabled;
    outPacket.decisionInput = decisionInput;
    outPacket.hudTelemetryInput = hudTelemetryInput;
    outPacket.facadeDecision = BuildPresenterFacadeDecisionPacket(decisionInput);
    outPacket.frameEndDecision = BuildPresenterFrameEndDecisionPacket(outPacket.facadeDecision);
    outPacket.hudTelemetryDecision = BuildPresenterHudTelemetryDecisionPacket(
        outPacket.facadeDecision,
        hudTelemetryInput);
}

inline PresenterFacadeDecisionBridgePacket BuildPresenterFacadeDecisionBridgePacket(
    const PresenterFacadeDecisionInputPacket& decisionInput,
    const PresenterHudTelemetryDecisionInputPacket& hudTelemetryInput)
{
    PresenterFacadeDecisionBridgePacket packet{};
    SeedPresenterFacadeDecisionBridgePacket(decisionInput, hudTelemetryInput, packet);
    return packet;
}

} // namespace GameLoopRuntime
