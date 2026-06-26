#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct TrackRenderTelemetryViewPacket
{
    bool valid = false;
    bool producerJobInFlight = false;
    bool producerSafeModeActive = false;
    uint16_t masterFrameTicks = 0u;
    uint16_t slaveProducerTicks = 0u;
    uint16_t slaveSortTicks = 0u;
    uint16_t slavePlanTicks = 0u;
    uint16_t queryCalls = 0u;
    uint16_t queryGlobal = 0u;
    uint16_t queryScmap = 0u;
    uint16_t queryCacheHits = 0u;
    uint16_t queryCacheMisses = 0u;
    uint16_t wallQueryCalls = 0u;
    uint16_t wallQueryHits = 0u;
};

} // namespace GameLoopRuntime
