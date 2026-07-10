#pragma once

#include "game_loop_presenter_boundary_text_contracts.hpp"
#include "game_loop_presenter_boundary_view_assembler.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterBoundaryStatusTextPacket(
    const PresenterBoundaryViewPacket& view,
    PresenterBoundaryStatusTextPacket& outPacket)
{
    outPacket.valid = view.valid;
    outPacket.gearChar = view.gearChar;
    outPacket.speedKmh = view.speedKmh;
    outPacket.engineRpm = view.engineRpm;
    outPacket.hasObservability = view.hasObservability;
    outPacket.hasSchedulerReuseDebug = view.hasSchedulerReuseDebug;
    outPacket.frameId = view.frameId;
}

inline PresenterBoundaryStatusTextPacket BuildPresenterBoundaryStatusTextPacket(
    const PresenterBoundaryViewPacket& view)
{
    PresenterBoundaryStatusTextPacket packet{};
    SeedPresenterBoundaryStatusTextPacket(view, packet);
    return packet;
}

inline void SeedPresenterBoundaryDecisionTextPacket(
    const PresenterBoundaryViewPacket& view,
    PresenterBoundaryDecisionTextPacket& outPacket)
{
    outPacket.valid = view.valid;
    outPacket.shouldPresentDrivingHud =
        view.presenceDecision.shouldPresentDrivingHud;
    outPacket.shouldPresentPeriodicHud =
        view.presenceDecision.shouldPresentPeriodicHud;
    outPacket.shouldPresentRenderDebug =
        view.presenceDecision.shouldPresentRenderDebug;
    outPacket.shouldPresentOverlayDebug =
        view.presenceDecision.shouldPresentOverlayDebug;
    outPacket.shouldPresentObservability =
        view.presenceDecision.shouldPresentObservability;
    outPacket.shouldPresentSchedulerReuseDebug =
        view.presenceDecision.shouldPresentSchedulerReuseDebug;
    outPacket.shouldPresentMemoryDebug =
        view.presenceDecision.shouldPresentMemoryDebug;
}

inline PresenterBoundaryDecisionTextPacket BuildPresenterBoundaryDecisionTextPacket(
    const PresenterBoundaryViewPacket& view)
{
    PresenterBoundaryDecisionTextPacket packet{};
    SeedPresenterBoundaryDecisionTextPacket(view, packet);
    return packet;
}

inline void SeedPresenterBoundaryTextPacket(
    const PresenterBoundaryViewPacket& view,
    PresenterBoundaryTextPacket& outPacket)
{
    outPacket.valid = view.valid;
    outPacket.status = BuildPresenterBoundaryStatusTextPacket(view);
    outPacket.decision = BuildPresenterBoundaryDecisionTextPacket(view);
}

inline PresenterBoundaryTextPacket BuildPresenterBoundaryTextPacket(
    const PresenterBoundaryViewPacket& view)
{
    PresenterBoundaryTextPacket packet{};
    SeedPresenterBoundaryTextPacket(view, packet);
    return packet;
}

inline PresenterBoundaryTextPacket BuildPresenterBoundaryTextPacket(
    const PresenterBoundaryPreviewPacket& preview)
{
    return BuildPresenterBoundaryTextPacket(
        BuildPresenterBoundaryViewPacket(preview));
}

inline PresenterBoundaryTextPacket BuildPresenterBoundaryTextPacket(
    const PresenterInputSummaryPacket& summary,
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const SchedulerReuseDebugTelemetryPacket& schedulerReuseDebug)
{
    return BuildPresenterBoundaryTextPacket(
        BuildPresenterBoundaryViewPacket(
            summary,
            overlay,
            observability,
            schedulerReuseDebug));
}

} // namespace GameLoopRuntime
