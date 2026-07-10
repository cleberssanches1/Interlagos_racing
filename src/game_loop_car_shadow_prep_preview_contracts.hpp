#pragma once

#include "car_render_system.hpp"

namespace GameLoopRuntime
{

struct CarShadowPrepPreviewPacket
{
    bool valid = false;
    Game::CarRenderSystem::RenderPacket renderPacket{};
    Game::CarRenderSystem::ShadowPacket shadowPacket{};
};

} // namespace GameLoopRuntime
