#pragma once

#include "game_loop_presenter_scheduler_reuse_preview_assembler.hpp"
#include "game_loop_presenter_summary_observability_assembler.hpp"
#include "game_loop_presenter_summary_scheduler_reuse_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterSummarySchedulerReusePreviewPacket(
    const PresenterSummaryObservabilityPacket& presenter,
    const PresenterSchedulerReusePreviewPacket& schedulerReuse,
    PresenterSummarySchedulerReusePreviewPacket& outPacket)
{
    outPacket.valid = presenter.valid || schedulerReuse.valid;
    outPacket.presenter = presenter;
    outPacket.schedulerReuse = schedulerReuse;
}

inline PresenterSummarySchedulerReusePreviewPacket BuildPresenterSummarySchedulerReusePreviewPacket(
    const PresenterSummaryObservabilityPacket& presenter,
    const PresenterSchedulerReusePreviewPacket& schedulerReuse)
{
    PresenterSummarySchedulerReusePreviewPacket packet{};
    SeedPresenterSummarySchedulerReusePreviewPacket(
        presenter,
        schedulerReuse,
        packet);
    return packet;
}

inline PresenterSummarySchedulerReusePreviewPacket BuildPresenterSummarySchedulerReusePreviewPacket(
    const PresenterInputSummaryPacket& summary,
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const SchedulerReuseDebugTelemetryPacket& schedulerReuseDebug)
{
    const auto observabilityInput = BuildPresenterObservabilityInputPacket(
        overlay,
        observability,
        schedulerReuseDebug);
    return BuildPresenterSummarySchedulerReusePreviewPacket(
        BuildPresenterSummaryObservabilityPacket(summary, observabilityInput),
        BuildPresenterSchedulerReusePreviewPacket(
            summary,
            observabilityInput,
            schedulerReuseDebug));
}

} // namespace GameLoopRuntime
