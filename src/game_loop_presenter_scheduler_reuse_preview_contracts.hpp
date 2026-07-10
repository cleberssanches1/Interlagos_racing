#pragma once

#include "game_loop_presenter_input_summary_contracts.hpp"
#include "game_loop_presenter_observability_input_contracts.hpp"
#include "game_loop_scheduler_reuse_debug_telemetry_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterSchedulerReusePreviewPacket
{
    bool valid = false;
    PresenterInputSummaryPacket summary{};
    PresenterObservabilityInputPacket observabilityInput{};
    SchedulerReuseDebugTelemetryPacket schedulerReuseDebug{};
};

} // namespace GameLoopRuntime
