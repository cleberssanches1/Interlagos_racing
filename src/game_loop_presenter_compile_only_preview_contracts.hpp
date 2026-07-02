#pragma once

#include "game_loop_presenter_frame_end_preview_contracts.hpp"
#include "game_loop_presenter_hud_telemetry_preview_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterCompileOnlyPreviewPacket
{
    bool valid = false;
    PresenterFrameEndPreviewPacket frameEnd{};
    PresenterHudTelemetryPreviewPacket hudTelemetry{};
};

} // namespace GameLoopRuntime
