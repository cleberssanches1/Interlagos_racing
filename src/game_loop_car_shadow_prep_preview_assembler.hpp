#pragma once

#include "game_loop_car_shadow_prep_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedCarShadowPrepPreviewPacket(
    const Game::CarRenderSystem::RenderPacket& renderPacket,
    const Game::CarRenderSystem::ShadowPacket& shadowPacket,
    CarShadowPrepPreviewPacket& outPacket)
{
    outPacket.valid = renderPacket.valid || shadowPacket.drawBlob || shadowPacket.drawModel;
    outPacket.renderPacket = renderPacket;
    outPacket.shadowPacket = shadowPacket;
}

inline CarShadowPrepPreviewPacket BuildCarShadowPrepPreviewPacket(
    const Game::CarRenderSystem::RenderPacket& renderPacket,
    const Game::CarRenderSystem::ShadowPacket& shadowPacket)
{
    CarShadowPrepPreviewPacket packet{};
    SeedCarShadowPrepPreviewPacket(renderPacket, shadowPacket, packet);
    return packet;
}

inline CarShadowPrepPreviewPacket BuildCarShadowPrepPreviewPacket(
    const Game::CarRenderSystem::RenderPacket& renderPacket,
    bool drawBlob,
    bool drawModel,
    int32_t groundBiasUnits)
{
    return BuildCarShadowPrepPreviewPacket(
        renderPacket,
        Game::CarRenderSystem::BuildShadowPacket(renderPacket,
                                                 drawBlob,
                                                 drawModel,
                                                 groundBiasUnits));
}

} // namespace GameLoopRuntime
