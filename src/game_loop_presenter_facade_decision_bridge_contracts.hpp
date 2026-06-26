#pragma once

#include "game_loop_presenter_facade_decision_input_contracts.hpp"
#include "game_loop_presenter_facade_interface_contracts.hpp"
#include "game_loop_presenter_frame_end_decision_contracts.hpp"
#include "game_loop_presenter_hud_telemetry_decision_contracts.hpp"
#include "game_loop_presenter_hud_telemetry_decision_input_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterFacadeDecisionBridgePacket
{
    bool valid = false;
    PresenterFacadeDecisionInputPacket decisionInput{};
    PresenterHudTelemetryDecisionInputPacket hudTelemetryInput{};
    PresenterFacadeDecisionPacket facadeDecision{};
    PresenterFrameEndDecisionPacket frameEndDecision{};
    PresenterHudTelemetryDecisionPacket hudTelemetryDecision{};
};

} // namespace GameLoopRuntime
