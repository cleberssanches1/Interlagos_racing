#pragma once

#include "game_loop_presenter_render_debug_contracts.hpp"
#include "game_loop_render_debug_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterRenderDebugPacket(const RenderFrameDebugBundle& renderBundle,
                                           PresenterRenderDebugPacket& outPacket)
{
    outPacket.valid = renderBundle.valid;
    outPacket.hasTrack = renderBundle.HasTrack();
    outPacket.hasCar = renderBundle.HasCar();
    outPacket.hasRenderableWork = renderBundle.HasRenderableWork();
    outPacket.track = renderBundle.trackDebug;
    outPacket.car = renderBundle.carDebug;
}

inline PresenterRenderDebugPacket BuildPresenterRenderDebugPacket(
    const RenderFrameDebugBundle& renderBundle)
{
    PresenterRenderDebugPacket packet{};
    SeedPresenterRenderDebugPacket(renderBundle, packet);
    return packet;
}

} // namespace GameLoopRuntime
