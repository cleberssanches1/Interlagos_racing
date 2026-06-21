#pragma once

#include "game_loop_observability_contracts.hpp"
#include "game_loop_overlay_debug_text_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct OverlayDebugBundle
{
    bool valid = false;
    OverlayPacketFlow overlayFlow{};
    TelemetryPacketFlow telemetryFlow{};
    OverlayDebugTextBundle text{};
};

} // namespace GameLoopObservabilityDomain
