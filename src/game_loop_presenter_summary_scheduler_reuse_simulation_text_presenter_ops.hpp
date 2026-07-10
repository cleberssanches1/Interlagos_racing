#pragma once

#include <srl.hpp>

#include "game_loop_presenter_summary_scheduler_reuse_simulation_text_assembler.hpp"

namespace GameLoopRuntime
{

inline void PresentPresenterSummarySchedulerReuseSimulationStatusTextPacket(
    const PresenterSummarySchedulerReuseSimulationStatusTextPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    SRL::Debug::Print(
        1,
        30,
        "PS %c %3d %5d o:%u s:%u sr:%u",
        packet.gearChar,
        static_cast<int>(packet.speedKmh),
        static_cast<int>(packet.engineRpm),
        static_cast<unsigned>(packet.hasObservability ? 1u : 0u),
        static_cast<unsigned>(packet.hasSchedulerReuseDebug ? 1u : 0u),
        static_cast<unsigned>(packet.hasSimulationReuseDebugPreview ? 1u : 0u));
}

inline void PresentPresenterSummarySchedulerReuseSimulationTextPacket(
    const PresenterSummarySchedulerReuseSimulationTextPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    PresentPresenterSummarySchedulerReuseSimulationStatusTextPacket(packet.status);
}

inline void PresentPresenterSummarySchedulerReuseSimulationTextPacket(
    const PresenterSummarySchedulerReuseSimulationViewPacket& packet)
{
    PresentPresenterSummarySchedulerReuseSimulationTextPacket(
        BuildPresenterSummarySchedulerReuseSimulationTextPacket(packet));
}

inline void PresentPresenterSummarySchedulerReuseSimulationTextPacket(
    const PresenterSummarySchedulerReuseSimulationPreviewPacket& packet)
{
    PresentPresenterSummarySchedulerReuseSimulationTextPacket(
        BuildPresenterSummarySchedulerReuseSimulationTextPacket(packet));
}

} // namespace GameLoopRuntime
