#pragma once

#include "auto_lap_route_contracts.hpp"

namespace GameLoopRuntime
{

struct AutoLapFramePacket
{
    AutoLapRouteDomain::AutoLapFrameContext frameContext{};
    AutoLapRouteDomain::AutoLapRouteStorageSnapshot storage{};
    AutoLapRouteDomain::AutoLapGuideLoadPacket guideLoad{};
    AutoLapRouteDomain::AutoLapRouteBuildPacket build{};
    AutoLapRouteDomain::AutoLapGuideRouteTrace routeTrace{};
    AutoLapRouteDomain::AutoLapRouteStepPacket step{};

    bool Valid() const
    {
        return storage.initialized ||
               storage.built ||
               guideLoad.attempted ||
               build.valid ||
               routeTrace.outputPointCount > 0u ||
               step.valid;
    }
};

} // namespace GameLoopRuntime
