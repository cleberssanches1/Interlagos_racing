#pragma once

#include "game_loop_presenter_hud_telemetry_decision_contracts.hpp"
#include "game_loop_presenter_hud_telemetry_decision_input_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterHudTelemetryPreviewPacket
{
    bool valid = false;
    PresenterHudTelemetryDecisionInputPacket inputs{};
    PresenterHudTelemetryDecisionPacket decision{};
};

} // namespace GameLoopRuntime
