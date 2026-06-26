#pragma once

#include "game_loop_render_budget_observability_view_contracts.hpp"
#include "game_loop_render_budget_presentation_view_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedRenderBudgetPresentationViewPacket(
    const RenderBudgetObservabilityViewPacket& observabilityView,
    RenderBudgetPresentationViewPacket& outPacket)
{
    outPacket.valid = observabilityView.valid;
    outPacket.trackPreferredPool = observabilityView.track.preferredPool;
    outPacket.carPreferredPool = observabilityView.car.preferredPool;
    outPacket.trackShouldReducePressure = observabilityView.track.shouldReducePressure;
    outPacket.carShouldReducePressure = observabilityView.car.shouldReducePressure;
    outPacket.trackShouldAvoidOptionalAllocations =
        observabilityView.track.shouldAvoidOptionalAllocations;
    outPacket.carShouldAvoidOptionalAllocations =
        observabilityView.car.shouldAvoidOptionalAllocations;
    outPacket.anyRenderShouldReducePressure =
        observabilityView.track.shouldReducePressure ||
        observabilityView.car.shouldReducePressure;
    outPacket.anyRenderShouldAvoidOptionalAllocations =
        observabilityView.track.shouldAvoidOptionalAllocations ||
        observabilityView.car.shouldAvoidOptionalAllocations;
}

inline RenderBudgetPresentationViewPacket BuildRenderBudgetPresentationViewPacket(
    const RenderBudgetObservabilityViewPacket& observabilityView)
{
    RenderBudgetPresentationViewPacket packet{};
    SeedRenderBudgetPresentationViewPacket(observabilityView, packet);
    return packet;
}

} // namespace GameLoopObservabilityDomain
