#pragma once

#include "car_render_system.hpp"

namespace GameLoopRuntime
{

struct CarVisualFramePacket
{
    Game::CarSystem* car = nullptr;
    int32_t syncYawDeg = 0;
    Game::CarRenderSystem::FrameContext frameContext{};
    Game::CarRenderSystem::RenderPacket renderPacket{};

    bool Valid() const
    {
        return (car != nullptr) && renderPacket.valid;
    }
};

} // namespace GameLoopRuntime
