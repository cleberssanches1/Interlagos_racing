#pragma once

#include "game_loop_presenter_boundary_preview_assembler.hpp"
#include "game_loop_presenter_boundary_view_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterBoundaryViewPacket(
    const PresenterBoundaryPreviewPacket& preview,
    PresenterBoundaryViewPacket& outPacket)
{
    outPacket.valid = preview.valid;
    outPacket.presenceDecision = preview.presence.decision;
    outPacket.hasSchedulerReuseDebug =
        preview.schedulerReuse.presenter.summary.hasSchedulerReuseDebug;
    outPacket.hasObservability =
        preview.schedulerReuse.presenter.observabilityInput.hasObservability;
    outPacket.speedKmh = preview.schedulerReuse.presenter.summary.speedKmh;
    outPacket.gearChar = preview.schedulerReuse.presenter.summary.gearChar;
    outPacket.engineRpm = preview.schedulerReuse.presenter.summary.engineRpm;
    outPacket.frameId = preview.schedulerReuse.presenter.summary.frameId;
}

inline PresenterBoundaryViewPacket BuildPresenterBoundaryViewPacket(
    const PresenterBoundaryPreviewPacket& preview)
{
    PresenterBoundaryViewPacket packet{};
    SeedPresenterBoundaryViewPacket(preview, packet);
    return packet;
}

inline PresenterBoundaryViewPacket BuildPresenterBoundaryViewPacket(
    const PresenterPresencePreviewPacket& presence,
    const PresenterSummarySchedulerReusePreviewPacket& schedulerReuse)
{
    return BuildPresenterBoundaryViewPacket(
        BuildPresenterBoundaryPreviewPacket(presence, schedulerReuse));
}

inline PresenterBoundaryViewPacket BuildPresenterBoundaryViewPacket(
    const PresenterInputSummaryPacket& summary,
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const SchedulerReuseDebugTelemetryPacket& schedulerReuseDebug)
{
    return BuildPresenterBoundaryViewPacket(
        BuildPresenterBoundaryPreviewPacket(
            summary,
            overlay,
            observability,
            schedulerReuseDebug));
}

} // namespace GameLoopRuntime
