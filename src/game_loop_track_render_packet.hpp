#pragma once

#include "track_render_scheduler.hpp"

namespace GameLoopRuntime
{

struct TrackRenderFramePacket
{
    Game::TrackRenderScheduler::FrameContext frameContext{};
    Game::TrackRenderScheduler::RenderPacket renderPacket{};
    Game::TrackRenderScheduler::Telemetry telemetry{};

    bool Valid() const
    {
        return renderPacket.valid;
    }
};

} // namespace GameLoopRuntime
