#pragma once

#include "game_loop_observability_debug_contracts.hpp"
#include "game_loop_overlay_debug_contracts.hpp"
#include "game_loop_presenter_overlay_debug_contracts.hpp"
#include "game_loop_scheduler_reuse_debug_telemetry_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterObservabilityInputPacket
{
    bool valid = false;
    GameLoopObservabilityDomain::OverlayDebugBundle overlay{};
    PresenterOverlayDebugPacket overlayDebug{};
    GameLoopObservabilityDomain::ObservabilityDebugBundle observability{};
    GameLoopTelemetryDomain::SchedulerReuseDebugTelemetryPacket schedulerReuse{};
};

} // namespace GameLoopRuntime
