#pragma once

#include "car_render_system.hpp"
#include "game_loop_runtime_state.hpp"

namespace GameLoopRuntime
{

inline Game::CarRenderSystem::RenderPacket BuildCarShadowRenderPacket(
    const CarRenderFrameState& carFrame)
{
    Game::CarRenderSystem::RenderPacket renderPacket{};
    renderPacket.valid = (carFrame.car != nullptr);
    renderPacket.renderPosition = carFrame.renderPosition;
    renderPacket.renderYawDeg = carFrame.renderYawDeg;
    renderPacket.runtimeDebug = carFrame.runtimeDebug;
    return renderPacket;
}

inline Game::CarRenderSystem::ShadowPacket BuildCarShadowPrepPacket(
    const CarRenderFrameState& carFrame,
    bool drawBlob,
    bool drawModel,
    int32_t groundBiasUnits)
{
    return Game::CarRenderSystem::BuildShadowPacket(
        BuildCarShadowRenderPacket(carFrame),
        drawBlob,
        drawModel,
        groundBiasUnits);
}

} // namespace GameLoopRuntime
