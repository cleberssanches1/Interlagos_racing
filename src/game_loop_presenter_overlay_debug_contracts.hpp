#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct PresenterOverlayDebugPacket
{
    bool valid = false;
    bool hasOverlayFlow = false;
    bool hasTelemetryFlow = false;
    bool hasOverlayText = false;
    bool hasFrameObservability = false;
    bool hasMemoryDebug = false;
    bool hasWorkRamUsage = false;
    bool hasLowWorkOverlay = false;
    bool hasHighWorkTrace = false;
    bool hasLowWorkTrace = false;
    bool carSegmentChanged = false;
    bool windowStartChanged = false;
    bool wallHit = false;
    uint32_t frameId = 0u;
    int16_t activeSegmentId = -1;
    int16_t nearestSegmentId = -1;
    int16_t windowStartId = -1;
    uint8_t windowCount = 0u;
    uint16_t groundMask = 0u;
    uint16_t submittedFacesTotal = 0u;
    uint16_t queryCalls = 0u;
    uint16_t wallQueryCalls = 0u;
};

} // namespace GameLoopRuntime
