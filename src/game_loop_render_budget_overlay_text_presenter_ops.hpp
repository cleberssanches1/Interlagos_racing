#pragma once

#include <srl.hpp>

#include "game_loop_render_budget_overlay_text_bridge_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline void PresentRenderBudgetOverlayTextViewPacket(const RenderBudgetOverlayTextViewPacket& packet)
{
    SRL::Debug::Print(2, 10, "RB1 tk:%u cr:%u any:%u red:%u",
                      static_cast<unsigned>(packet.shouldShowTrackBudget ? 1u : 0u),
                      static_cast<unsigned>(packet.shouldShowCarBudget ? 1u : 0u),
                      static_cast<unsigned>(packet.shouldShowAnyRenderBudget ? 1u : 0u),
                      static_cast<unsigned>(packet.shouldShowAnyReducePressure ? 1u : 0u));
    SRL::Debug::Print(2, 11, "RB2 grd:%u tp:%u cp:%u   ",
                      static_cast<unsigned>(packet.shouldShowAnyOptionalAllocationGuard ? 1u : 0u),
                      static_cast<unsigned>(packet.trackPreferredPool),
                      static_cast<unsigned>(packet.carPreferredPool));
}

inline void PresentRenderBudgetOverlayTextViewPacket(
    const GameLoopRuntime::TrackRenderFramePacket& trackPacket,
    const GameLoopRuntime::CarVisualFramePacket& carPacket)
{
    PresentRenderBudgetOverlayTextViewPacket(
        BuildRenderBudgetOverlayTextViewPacket(trackPacket, carPacket));
}

} // namespace GameLoopObservabilityDomain
