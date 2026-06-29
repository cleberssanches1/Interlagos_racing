#pragma once

#include "game_loop_presenter_facade_decision_bridge_assembler.hpp"
#include "game_loop_presenter_hud_telemetry_decision_assembler.hpp"
#include "game_loop_presenter_hud_telemetry_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterHudTelemetryPreviewPacket(
    const PresenterHudTelemetryDecisionInputPacket& inputs,
    const PresenterHudTelemetryDecisionPacket& decision,
    PresenterHudTelemetryPreviewPacket& outPacket)
{
    outPacket.valid = decision.valid || inputs.runtimeStatsEnabled;
    outPacket.inputs = inputs;
    outPacket.decision = decision;
}

inline PresenterHudTelemetryPreviewPacket BuildPresenterHudTelemetryPreviewPacket(
    const PresenterHudTelemetryDecisionInputPacket& inputs,
    const PresenterHudTelemetryDecisionPacket& decision)
{
    PresenterHudTelemetryPreviewPacket packet{};
    SeedPresenterHudTelemetryPreviewPacket(inputs, decision, packet);
    return packet;
}

inline PresenterHudTelemetryPreviewPacket BuildPresenterHudTelemetryPreviewPacket(
    const PresenterFacadeDecisionPacket& facadeDecision,
    const PresenterHudTelemetryDecisionInputPacket& inputs)
{
    return BuildPresenterHudTelemetryPreviewPacket(
        inputs,
        BuildPresenterHudTelemetryDecisionPacket(facadeDecision, inputs));
}

inline PresenterHudTelemetryPreviewPacket BuildPresenterHudTelemetryPreviewPacket(
    const PresenterFacadeDecisionBridgePacket& bridge)
{
    return BuildPresenterHudTelemetryPreviewPacket(
        bridge.hudTelemetryInput,
        bridge.hudTelemetryDecision);
}

} // namespace GameLoopRuntime
