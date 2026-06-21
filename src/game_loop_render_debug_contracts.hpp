#pragma once

#include "game_loop_car_visual_debug_contracts.hpp"
#include "game_loop_car_visual_packet.hpp"
#include "game_loop_track_render_debug_contracts.hpp"
#include "game_loop_track_render_packet.hpp"

namespace GameLoopRuntime
{

struct RenderFrameDebugBundle
{
    bool valid = false;
    TrackRenderFramePacket track{};
    TrackRenderDebugPacket trackDebug{};
    CarVisualFramePacket car{};
    CarVisualDebugPacket carDebug{};

    bool HasTrack() const
    {
        return track.Valid();
    }

    bool HasCar() const
    {
        return car.Valid();
    }

    bool HasRenderableWork() const
    {
        return HasTrack() || HasCar();
    }
};

} // namespace GameLoopRuntime
