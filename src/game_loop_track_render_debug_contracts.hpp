#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct TrackRenderDebugPacket
{
    bool valid = false;
    bool renderEnabled = false;
    bool usedSlaveProducer = false;
    bool usedSlaveSort = false;
    bool producerSafeModeActive = false;
    bool producerJobInFlight = false;
    bool shouldReducePressure = false;
    bool shouldAvoidOptionalAllocations = false;
    uint32_t frameId = 0u;
    int32_t observedCarSegmentId = -1;
    uint16_t submittedTrackFaces = 0u;
    uint16_t queryCalls = 0u;
    uint16_t wallQueryCalls = 0u;
};

} // namespace GameLoopRuntime
