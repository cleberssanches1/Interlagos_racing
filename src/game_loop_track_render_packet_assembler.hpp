#pragma once

#include "game_loop_track_render_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedTrackRenderFramePacket(
    const Game::TrackRenderScheduler::FrameContext& frameContext,
    const Game::TrackRenderScheduler::RenderPacket& renderPacket,
    const Game::TrackRenderScheduler::Telemetry& telemetry,
    TrackRenderFramePacket& outPacket)
{
    outPacket.frameContext = frameContext;
    outPacket.renderPacket = renderPacket;
    outPacket.telemetry = telemetry;
}

} // namespace GameLoopRuntime
