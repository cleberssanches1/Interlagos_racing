#pragma once

#include "game_loop_presenter_presence_decision_assembler.hpp"
#include "game_loop_presenter_presence_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterPresencePreviewPacket(
    const PresenterSummaryObservabilityPacket& presenter,
    const PresenterPresenceDecisionPacket& decision,
    PresenterPresencePreviewPacket& outPacket)
{
    outPacket.valid = presenter.valid || decision.valid;
    outPacket.presenter = presenter;
    outPacket.decision = decision;
}

inline PresenterPresencePreviewPacket BuildPresenterPresencePreviewPacket(
    const PresenterSummaryObservabilityPacket& presenter,
    const PresenterPresenceDecisionPacket& decision)
{
    PresenterPresencePreviewPacket packet{};
    SeedPresenterPresencePreviewPacket(presenter, decision, packet);
    return packet;
}

inline PresenterPresencePreviewPacket BuildPresenterPresencePreviewPacket(
    const PresenterSummaryObservabilityPacket& presenter)
{
    return BuildPresenterPresencePreviewPacket(
        presenter,
        BuildPresenterPresenceDecisionPacket(presenter));
}

inline PresenterPresencePreviewPacket BuildPresenterPresencePreviewPacket(
    const PresenterInputBundle& inputBundle)
{
    return BuildPresenterPresencePreviewPacket(
        BuildPresenterSummaryObservabilityPacket(inputBundle));
}

} // namespace GameLoopRuntime
