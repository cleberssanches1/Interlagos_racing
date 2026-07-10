#pragma once

#include "game_loop_presenter_summary_scheduler_reuse_simulation_text_contracts.hpp"
#include "game_loop_presenter_summary_scheduler_reuse_simulation_view_assembler.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterSummarySchedulerReuseSimulationStatusTextPacket(
    const PresenterSummarySchedulerReuseSimulationViewPacket& view,
    PresenterSummarySchedulerReuseSimulationStatusTextPacket& outPacket)
{
    outPacket.valid = view.valid;
    outPacket.gearChar = view.gearChar;
    outPacket.speedKmh = view.speedKmh;
    outPacket.engineRpm = view.engineRpm;
    outPacket.hasObservability = view.hasObservability;
    outPacket.hasSchedulerReuseDebug = view.hasSchedulerReuseDebug;
    outPacket.hasSimulationReuseDebugPreview = view.hasSimulationReuseDebugPreview;
    outPacket.frameId = view.frameId;
}

inline PresenterSummarySchedulerReuseSimulationStatusTextPacket
BuildPresenterSummarySchedulerReuseSimulationStatusTextPacket(
    const PresenterSummarySchedulerReuseSimulationViewPacket& view)
{
    PresenterSummarySchedulerReuseSimulationStatusTextPacket packet{};
    SeedPresenterSummarySchedulerReuseSimulationStatusTextPacket(view, packet);
    return packet;
}

inline void SeedPresenterSummarySchedulerReuseSimulationTextPacket(
    const PresenterSummarySchedulerReuseSimulationViewPacket& view,
    PresenterSummarySchedulerReuseSimulationTextPacket& outPacket)
{
    outPacket.valid = view.valid;
    outPacket.status =
        BuildPresenterSummarySchedulerReuseSimulationStatusTextPacket(view);
}

inline PresenterSummarySchedulerReuseSimulationTextPacket
BuildPresenterSummarySchedulerReuseSimulationTextPacket(
    const PresenterSummarySchedulerReuseSimulationViewPacket& view)
{
    PresenterSummarySchedulerReuseSimulationTextPacket packet{};
    SeedPresenterSummarySchedulerReuseSimulationTextPacket(view, packet);
    return packet;
}

inline PresenterSummarySchedulerReuseSimulationTextPacket
BuildPresenterSummarySchedulerReuseSimulationTextPacket(
    const PresenterSummarySchedulerReuseSimulationPreviewPacket& preview)
{
    return BuildPresenterSummarySchedulerReuseSimulationTextPacket(
        BuildPresenterSummarySchedulerReuseSimulationViewPacket(preview));
}

} // namespace GameLoopRuntime
