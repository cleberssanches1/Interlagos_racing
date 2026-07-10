#pragma once

#include "game_loop_presenter_summary_scheduler_reuse_simulation_preview_assembler.hpp"
#include "game_loop_presenter_summary_scheduler_reuse_simulation_view_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterSummarySchedulerReuseSimulationViewPacket(
    const PresenterSummarySchedulerReuseSimulationPreviewPacket& preview,
    PresenterSummarySchedulerReuseSimulationViewPacket& outPacket)
{
    outPacket.valid = preview.valid;
    outPacket.hasObservability =
        preview.presenterSchedulerReuse.presenter.observabilityInput.hasObservability;
    outPacket.hasSchedulerReuseDebug =
        preview.presenterSchedulerReuse.presenter.summary.hasSchedulerReuseDebug;
    outPacket.hasSimulationReuseDebugPreview =
        preview.schedulerReuseSimulation.simulationDebugPreview.valid;
    outPacket.speedKmh =
        preview.presenterSchedulerReuse.presenter.summary.speedKmh;
    outPacket.gearChar =
        preview.presenterSchedulerReuse.presenter.summary.gearChar;
    outPacket.engineRpm =
        preview.presenterSchedulerReuse.presenter.summary.engineRpm;
    outPacket.frameId =
        preview.presenterSchedulerReuse.presenter.summary.frameId;
}

inline PresenterSummarySchedulerReuseSimulationViewPacket
BuildPresenterSummarySchedulerReuseSimulationViewPacket(
    const PresenterSummarySchedulerReuseSimulationPreviewPacket& preview)
{
    PresenterSummarySchedulerReuseSimulationViewPacket packet{};
    SeedPresenterSummarySchedulerReuseSimulationViewPacket(preview, packet);
    return packet;
}

} // namespace GameLoopRuntime
