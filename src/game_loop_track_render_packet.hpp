#pragma once

#include "memory_budget_contracts.hpp"
#include "track_render_scheduler.hpp"

namespace GameLoopRuntime
{

struct TrackRenderFramePacket
{
    Game::TrackRenderScheduler::FrameContext frameContext{};
    Game::TrackRenderScheduler::RenderPacket renderPacket{};
    Game::TrackRenderScheduler::Telemetry telemetry{};
    MemoryBudgetDomain::CategoryBudgetPolicy budgetPolicy{};

    bool Valid() const
    {
        return renderPacket.valid;
    }
};

} // namespace GameLoopRuntime
