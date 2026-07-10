#pragma once

#include "game_loop_presenter_summary_scheduler_reuse_preview_assembler.hpp"
#include "game_loop_presenter_summary_scheduler_reuse_simulation_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterSummarySchedulerReuseSimulationPreviewPacket(
    const PresenterSummarySchedulerReusePreviewPacket& presenterSchedulerReuse,
    const GameLoopObservabilityDomain::SchedulerReuseSimulationPreviewPacket&
        schedulerReuseSimulation,
    PresenterSummarySchedulerReuseSimulationPreviewPacket& outPacket)
{
    outPacket.valid = presenterSchedulerReuse.valid || schedulerReuseSimulation.valid;
    outPacket.presenterSchedulerReuse = presenterSchedulerReuse;
    outPacket.schedulerReuseSimulation = schedulerReuseSimulation;
}

inline PresenterSummarySchedulerReuseSimulationPreviewPacket
BuildPresenterSummarySchedulerReuseSimulationPreviewPacket(
    const PresenterSummarySchedulerReusePreviewPacket& presenterSchedulerReuse,
    const GameLoopObservabilityDomain::SchedulerReuseSimulationPreviewPacket&
        schedulerReuseSimulation)
{
    PresenterSummarySchedulerReuseSimulationPreviewPacket packet{};
    SeedPresenterSummarySchedulerReuseSimulationPreviewPacket(
        presenterSchedulerReuse,
        schedulerReuseSimulation,
        packet);
    return packet;
}

inline PresenterSummarySchedulerReuseSimulationPreviewPacket
BuildPresenterSummarySchedulerReuseSimulationPreviewPacket(
    const PresenterSummaryObservabilityPacket& presenter,
    const PresenterSchedulerReusePreviewPacket& schedulerReuse,
    const GameLoopObservabilityDomain::SchedulerReuseSimulationPreviewPacket&
        schedulerReuseSimulation)
{
    return BuildPresenterSummarySchedulerReuseSimulationPreviewPacket(
        BuildPresenterSummarySchedulerReusePreviewPacket(presenter, schedulerReuse),
        schedulerReuseSimulation);
}

} // namespace GameLoopRuntime
