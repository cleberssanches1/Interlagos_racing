#pragma once

#include "memory_budget_contracts.hpp"
#include "car_render_system.hpp"

namespace GameLoopRuntime
{

struct CarVisualFramePacket
{
    Game::CarSystem* car = nullptr;
    int32_t syncYawDeg = 0;
    Game::CarRenderSystem::FrameContext frameContext{};
    Game::CarRenderSystem::RenderPacket renderPacket{};
    Game::CarRenderSystem::ShadowPacket shadowPacket{};
    Game::CarRenderSystem::SubmitPacket submitPacket{};
    Game::CarRenderSystem::Telemetry telemetry{};
    MemoryBudgetDomain::CategoryBudgetPolicy budgetPolicy{};

    bool Valid() const
    {
        return (car != nullptr) && renderPacket.valid;
    }

    bool ReadyForSubmit() const
    {
        return Valid() && submitPacket.valid;
    }
};

} // namespace GameLoopRuntime
