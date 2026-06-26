#pragma once

#include "game_loop_presenter_facade_bridge_contracts.hpp"
#include "game_loop_presenter_facade_decision_bridge_contracts.hpp"
#include "game_loop_presenter_facade_interface_contracts.hpp"
#include "game_loop_presenter_hud_telemetry_decision_contracts.hpp"
#include "game_loop_presenter_hud_telemetry_decision_input_contracts.hpp"

namespace GameLoopRuntime
{

using PresenterHudTelemetryDecisionInputs = PresenterHudTelemetryDecisionInputPacket;

inline void SeedPresenterHudTelemetryDecisionPacket(
    const PresenterFacadeDecisionPacket& decision,
    const PresenterHudTelemetryDecisionInputPacket& inputs,
    PresenterHudTelemetryDecisionPacket& outPacket)
{
    outPacket.valid = decision.valid || inputs.runtimeStatsEnabled;
    outPacket.shouldPresentPeriodicHud =
        decision.shouldPresentPeriodicHud && !inputs.avoidOptionalHudTelemetry;
    const bool shouldPresentRuntimeTelemetry =
        inputs.runtimeStatsEnabled && !inputs.avoidDebugTransientOptionalTelemetry;
    outPacket.shouldPresentSegmentOverlapDiagnostics = shouldPresentRuntimeTelemetry;
    outPacket.shouldPresentSh2Telemetry = shouldPresentRuntimeTelemetry;
}

inline PresenterHudTelemetryDecisionPacket BuildPresenterHudTelemetryDecisionPacket(
    const PresenterFacadeDecisionPacket& decision,
    const PresenterHudTelemetryDecisionInputPacket& inputs)
{
    PresenterHudTelemetryDecisionPacket packet{};
    SeedPresenterHudTelemetryDecisionPacket(decision, inputs, packet);
    return packet;
}

inline PresenterHudTelemetryDecisionPacket BuildPresenterHudTelemetryDecisionPacket(
    const PresenterFacadeBridgePacket& bridge,
    const PresenterHudTelemetryDecisionInputPacket& inputs)
{
    return BuildPresenterHudTelemetryDecisionPacket(bridge.decision, inputs);
}

inline PresenterHudTelemetryDecisionPacket BuildPresenterHudTelemetryDecisionPacket(
    const PresenterFacadeDecisionBridgePacket& bridge)
{
    return bridge.hudTelemetryDecision;
}

} // namespace GameLoopRuntime
