#pragma once

#include "game_loop_presenter_facade_bridge_contracts.hpp"
#include "game_loop_presenter_facade_interface_contracts.hpp"
#include "game_loop_presenter_hud_telemetry_decision_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterHudTelemetryDecisionInputs
{
    bool runtimeStatsEnabled = false;
    bool avoidOptionalHudTelemetry = false;
    bool avoidDebugTransientOptionalTelemetry = false;
};

inline void SeedPresenterHudTelemetryDecisionPacket(
    const PresenterFacadeDecisionPacket& decision,
    const PresenterHudTelemetryDecisionInputs& inputs,
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
    const PresenterHudTelemetryDecisionInputs& inputs)
{
    PresenterHudTelemetryDecisionPacket packet{};
    SeedPresenterHudTelemetryDecisionPacket(decision, inputs, packet);
    return packet;
}

inline PresenterHudTelemetryDecisionPacket BuildPresenterHudTelemetryDecisionPacket(
    const PresenterFacadeBridgePacket& bridge,
    const PresenterHudTelemetryDecisionInputs& inputs)
{
    return BuildPresenterHudTelemetryDecisionPacket(bridge.decision, inputs);
}

} // namespace GameLoopRuntime
