#pragma once

#include "game_loop_presenter_presence_decision_contracts.hpp"
#include "game_loop_presenter_summary_observability_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterPresenceDecisionPacket(
    const PresenterSummaryObservabilityPacket& presenter,
    PresenterPresenceDecisionPacket& outPacket)
{
    outPacket.valid = presenter.valid;
    outPacket.shouldPresentDrivingHud = presenter.summary.hasDrivingHud;
    outPacket.shouldPresentPeriodicHud = presenter.summary.hasPeriodicHud;
    outPacket.shouldPresentRenderDebug = presenter.summary.hasRender;
    outPacket.shouldPresentOverlayDebug = presenter.summary.hasOverlay;
    outPacket.shouldPresentObservability = presenter.summary.hasObservability;
    outPacket.shouldPresentSchedulerReuseDebug =
        presenter.summary.hasSchedulerReuseDebug;
    outPacket.shouldPresentMemoryDebug = presenter.summary.hasMemoryDebug;
}

inline PresenterPresenceDecisionPacket BuildPresenterPresenceDecisionPacket(
    const PresenterSummaryObservabilityPacket& presenter)
{
    PresenterPresenceDecisionPacket packet{};
    SeedPresenterPresenceDecisionPacket(presenter, packet);
    return packet;
}

} // namespace GameLoopRuntime
