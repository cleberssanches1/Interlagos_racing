#pragma once

#include "game_loop_car_visual_packet.hpp"
#include "game_loop_observability_contracts.hpp"
#include "game_loop_render_budget_observability_view_contracts.hpp"
#include "game_loop_track_render_packet.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedRenderBudgetConsumerViewPacket(
    const RenderBudgetPolicyPacket& policyPacket,
    RenderBudgetConsumerViewPacket& outPacket)
{
    outPacket.valid = policyPacket.valid;
    outPacket.preferredPool = policyPacket.budgetPolicy.preferredPool;
    outPacket.shouldReducePressure = policyPacket.shouldReducePressure;
    outPacket.shouldAvoidOptionalAllocations = policyPacket.shouldAvoidOptionalAllocations;
}

inline RenderBudgetConsumerViewPacket BuildRenderBudgetConsumerViewPacket(
    const RenderBudgetPolicyPacket& policyPacket)
{
    RenderBudgetConsumerViewPacket packet{};
    SeedRenderBudgetConsumerViewPacket(policyPacket, packet);
    return packet;
}

inline void SeedRenderBudgetObservabilityViewPacket(
    const RenderBudgetPacketFlow& renderBudgetFlow,
    RenderBudgetObservabilityViewPacket& outPacket)
{
    outPacket.valid = renderBudgetFlow.valid;
    SeedRenderBudgetConsumerViewPacket(renderBudgetFlow.track, outPacket.track);
    SeedRenderBudgetConsumerViewPacket(renderBudgetFlow.car, outPacket.car);
}

inline void SeedRenderBudgetObservabilityViewPacket(
    const GameLoopRuntime::TrackRenderFramePacket& trackPacket,
    const GameLoopRuntime::CarVisualFramePacket& carPacket,
    RenderBudgetObservabilityViewPacket& outPacket)
{
    outPacket.valid = trackPacket.Valid() || carPacket.Valid();
    outPacket.track.valid = trackPacket.Valid();
    outPacket.track.preferredPool = trackPacket.budgetPolicy.preferredPool;
    outPacket.track.shouldReducePressure = trackPacket.budgetPolicy.shouldReducePressure;
    outPacket.track.shouldAvoidOptionalAllocations =
        trackPacket.budgetPolicy.shouldAvoidOptionalAllocations;
    outPacket.car.valid = carPacket.Valid();
    outPacket.car.preferredPool = carPacket.budgetPolicy.preferredPool;
    outPacket.car.shouldReducePressure = carPacket.budgetPolicy.shouldReducePressure;
    outPacket.car.shouldAvoidOptionalAllocations =
        carPacket.budgetPolicy.shouldAvoidOptionalAllocations;
}

inline RenderBudgetObservabilityViewPacket BuildRenderBudgetObservabilityViewPacket(
    const RenderBudgetPacketFlow& renderBudgetFlow)
{
    RenderBudgetObservabilityViewPacket packet{};
    SeedRenderBudgetObservabilityViewPacket(renderBudgetFlow, packet);
    return packet;
}

inline RenderBudgetObservabilityViewPacket BuildRenderBudgetObservabilityViewPacket(
    const GameLoopRuntime::TrackRenderFramePacket& trackPacket,
    const GameLoopRuntime::CarVisualFramePacket& carPacket)
{
    RenderBudgetObservabilityViewPacket packet{};
    SeedRenderBudgetObservabilityViewPacket(trackPacket, carPacket, packet);
    return packet;
}

} // namespace GameLoopObservabilityDomain
