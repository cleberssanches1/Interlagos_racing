#pragma once

#include "game_loop_auto_lap_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedAutoLapFramePacket(
    const AutoLapRouteDomain::AutoLapFrameContext& frameContext,
    const AutoLapRouteDomain::AutoLapRouteStorageSnapshot& storage,
    const AutoLapRouteDomain::AutoLapGuideLoadPacket& guideLoad,
    const AutoLapRouteDomain::AutoLapRouteBuildPacket& build,
    const AutoLapRouteDomain::AutoLapRouteStepPacket& step,
    AutoLapFramePacket& outPacket)
{
    outPacket.frameContext = frameContext;
    outPacket.storage = storage;
    outPacket.guideLoad = guideLoad;
    outPacket.build = build;
    outPacket.step = step;
}

} // namespace GameLoopRuntime
