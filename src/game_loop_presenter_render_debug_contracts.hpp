#pragma once

#include "game_loop_car_visual_debug_contracts.hpp"
#include "game_loop_track_render_debug_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterRenderDebugPacket
{
    bool valid = false;
    bool hasTrack = false;
    bool hasCar = false;
    bool hasRenderableWork = false;
    TrackRenderDebugPacket track{};
    CarVisualDebugPacket car{};
};

} // namespace GameLoopRuntime
