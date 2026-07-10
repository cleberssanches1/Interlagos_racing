#ifndef GAME_LOOP_PRESENTER_BOUNDARY_PREVIEW_ASSEMBLER_HPP
#define GAME_LOOP_PRESENTER_BOUNDARY_PREVIEW_ASSEMBLER_HPP

#include "game_loop_presenter_presence_preview_assembler.hpp"
#include "game_loop_presenter_summary_scheduler_reuse_preview_assembler.hpp"
#include "game_loop_presenter_boundary_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterBoundaryPreviewPacket(
    const PresenterPresencePreviewPacket& presence,
    const PresenterSummarySchedulerReusePreviewPacket& schedulerReuse,
    PresenterBoundaryPreviewPacket& outPacket)
{
    outPacket.valid = presence.valid || schedulerReuse.valid;
    outPacket.presence = presence;
    outPacket.schedulerReuse = schedulerReuse;
}

inline PresenterBoundaryPreviewPacket BuildPresenterBoundaryPreviewPacket(
    const PresenterPresencePreviewPacket& presence,
    const PresenterSummarySchedulerReusePreviewPacket& schedulerReuse)
{
    PresenterBoundaryPreviewPacket packet{};
    SeedPresenterBoundaryPreviewPacket(presence, schedulerReuse, packet);
    return packet;
}

inline PresenterBoundaryPreviewPacket BuildPresenterBoundaryPreviewPacket(
    const PresenterInputSummaryPacket& summary,
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const SchedulerReuseDebugTelemetryPacket& schedulerReuseDebug)
{
    const auto observabilityInput = BuildPresenterObservabilityInputPacket(
        overlay,
        observability,
        schedulerReuseDebug);
    const auto presenterSummary = BuildPresenterSummaryObservabilityPacket(
        summary,
        observabilityInput);
    return BuildPresenterBoundaryPreviewPacket(
        BuildPresenterPresencePreviewPacket(presenterSummary),
        BuildPresenterSummarySchedulerReusePreviewPacket(
            presenterSummary,
            BuildPresenterSchedulerReusePreviewPacket(
                summary,
                observabilityInput,
                schedulerReuseDebug)));
}

}

#endif
