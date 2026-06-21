#pragma once

#include "game_loop_car_visual_debug_assembler.hpp"
#include "game_loop_render_debug_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedRenderFrameDebugBundle(const TrackRenderFramePacket& track,
                                       const CarVisualFramePacket& car,
                                       RenderFrameDebugBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.track = track;
    outBundle.car = car;
    outBundle.carDebug = BuildCarVisualDebugPacket(car);
}

inline RenderFrameDebugBundle BuildRenderFrameDebugBundle(const TrackRenderFramePacket& track,
                                                          const CarVisualFramePacket& car)
{
    RenderFrameDebugBundle bundle{};
    SeedRenderFrameDebugBundle(track, car, bundle);
    return bundle;
}

} // namespace GameLoopRuntime
