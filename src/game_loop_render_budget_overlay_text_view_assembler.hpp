#pragma once

#include "game_loop_render_budget_overlay_text_view_contracts.hpp"
#include "game_loop_render_budget_presentation_view_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedRenderBudgetOverlayTextViewPacket(
    const RenderBudgetPresentationViewPacket& presentationView,
    RenderBudgetOverlayTextViewPacket& outPacket)
{
    outPacket.valid = presentationView.valid;
    outPacket.shouldShowTrackBudget = presentationView.trackShouldReducePressure ||
                                      presentationView.trackShouldAvoidOptionalAllocations;
    outPacket.shouldShowCarBudget = presentationView.carShouldReducePressure ||
                                    presentationView.carShouldAvoidOptionalAllocations;
    outPacket.shouldShowAnyRenderBudget =
        outPacket.shouldShowTrackBudget || outPacket.shouldShowCarBudget;
    outPacket.shouldShowAnyReducePressure = presentationView.anyRenderShouldReducePressure;
    outPacket.shouldShowAnyOptionalAllocationGuard =
        presentationView.anyRenderShouldAvoidOptionalAllocations;
    outPacket.trackPreferredPool = presentationView.trackPreferredPool;
    outPacket.carPreferredPool = presentationView.carPreferredPool;
}

inline RenderBudgetOverlayTextViewPacket BuildRenderBudgetOverlayTextViewPacket(
    const RenderBudgetPresentationViewPacket& presentationView)
{
    RenderBudgetOverlayTextViewPacket packet{};
    SeedRenderBudgetOverlayTextViewPacket(presentationView, packet);
    return packet;
}

} // namespace GameLoopObservabilityDomain
