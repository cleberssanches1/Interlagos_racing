#pragma once

#include "game_loop_presenter_scheduler_reuse_preview_contracts.hpp"
#include "game_loop_presenter_summary_observability_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterSummarySchedulerReusePreviewPacket
{
    bool valid = false;
    PresenterSummaryObservabilityPacket presenter{};
    PresenterSchedulerReusePreviewPacket schedulerReuse{};
};

} // namespace GameLoopRuntime
