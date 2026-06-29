#pragma once

#include "game_loop_presenter_facade_decision_bridge_assembler.hpp"
#include "game_loop_presenter_frame_end_decision_assembler.hpp"
#include "game_loop_presenter_frame_end_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterFrameEndPreviewPacket(
    const PresenterFacadeDecisionInputPacket& decisionInput,
    const PresenterFrameEndDecisionPacket& decision,
    PresenterFrameEndPreviewPacket& outPacket)
{
    outPacket.valid = decision.valid || decisionInput.valid;
    outPacket.decisionInput = decisionInput;
    outPacket.decision = decision;
}

inline PresenterFrameEndPreviewPacket BuildPresenterFrameEndPreviewPacket(
    const PresenterFacadeDecisionInputPacket& decisionInput,
    const PresenterFrameEndDecisionPacket& decision)
{
    PresenterFrameEndPreviewPacket packet{};
    SeedPresenterFrameEndPreviewPacket(decisionInput, decision, packet);
    return packet;
}

inline PresenterFrameEndPreviewPacket BuildPresenterFrameEndPreviewPacket(
    const PresenterFacadeDecisionPacket& facadeDecision,
    const PresenterFacadeDecisionInputPacket& decisionInput)
{
    return BuildPresenterFrameEndPreviewPacket(
        decisionInput,
        BuildPresenterFrameEndDecisionPacket(facadeDecision));
}

inline PresenterFrameEndPreviewPacket BuildPresenterFrameEndPreviewPacket(
    const PresenterFacadeDecisionBridgePacket& bridge)
{
    return BuildPresenterFrameEndPreviewPacket(
        bridge.decisionInput,
        bridge.frameEndDecision);
}

} // namespace GameLoopRuntime
