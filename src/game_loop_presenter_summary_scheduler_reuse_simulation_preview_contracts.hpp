#pragma once

#include "game_loop_presenter_summary_scheduler_reuse_preview_contracts.hpp"
#include "game_loop_scheduler_reuse_simulation_preview_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterSummarySchedulerReuseSimulationPreviewPacket
{
    bool valid = false;
    PresenterSummarySchedulerReusePreviewPacket presenterSchedulerReuse{};
    GameLoopObservabilityDomain::SchedulerReuseSimulationPreviewPacket
        schedulerReuseSimulation{};
};

} // namespace GameLoopRuntime
