#pragma once

#include "game_loop_car_visual_packet.hpp"
#include "game_loop_render_budget_observability_view_assembler.hpp"
#include "game_loop_render_budget_overlay_text_view_assembler.hpp"
#include "game_loop_render_budget_presentation_view_assembler.hpp"
#include "game_loop_track_render_packet.hpp"

namespace GameLoopObservabilityDomain
{

inline RenderBudgetPresentationViewPacket BuildRenderBudgetPresentationViewPacket(
    const GameLoopRuntime::TrackRenderFramePacket& trackPacket,
    const GameLoopRuntime::CarVisualFramePacket& carPacket)
{
    return BuildRenderBudgetPresentationViewPacket(
        BuildRenderBudgetObservabilityViewPacket(trackPacket, carPacket));
}

inline RenderBudgetOverlayTextViewPacket BuildRenderBudgetOverlayTextViewPacket(
    const GameLoopRuntime::TrackRenderFramePacket& trackPacket,
    const GameLoopRuntime::CarVisualFramePacket& carPacket)
{
    return BuildRenderBudgetOverlayTextViewPacket(
        BuildRenderBudgetPresentationViewPacket(trackPacket, carPacket));
}

} // namespace GameLoopObservabilityDomain
