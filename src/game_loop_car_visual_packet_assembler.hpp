#pragma once

#include "memory_budget_runtime_bridge.hpp"
#include "game_loop_car_visual_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedCarVisualFramePacket(Game::CarSystem* car,
                                     int32_t syncYawDeg,
                                     const Game::CarRenderSystem::FrameContext& frameContext,
                                     const Game::CarRenderSystem::RenderPacket& renderPacket,
                                     const Game::CarRenderSystem::ShadowPacket& shadowPacket,
                                     const Game::CarRenderSystem::SubmitPacket& submitPacket,
                                     const Game::CarRenderSystem::Telemetry& telemetry,
                                     CarVisualFramePacket& outPacket)
{
    outPacket.car = car;
    outPacket.syncYawDeg = syncYawDeg;
    outPacket.frameContext = frameContext;
    outPacket.renderPacket = renderPacket;
    outPacket.shadowPacket = shadowPacket;
    outPacket.submitPacket = submitPacket;
    outPacket.telemetry = telemetry;
    outPacket.budgetPolicy = Game::MemoryBudgetRuntimeBridge::QueryCategoryPolicy(
        Game::MemoryBudgetRuntimeBridge::ConsumerCategory::CarRender);
}

inline bool CanSubmitCarVisualFramePacket(const CarVisualFramePacket& packet)
{
    return packet.ReadyForSubmit();
}

} // namespace GameLoopRuntime
