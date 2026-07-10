#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct PresenterSummarySchedulerReuseSimulationViewPacket
{
    bool valid = false;
    bool hasObservability = false;
    bool hasSchedulerReuseDebug = false;
    bool hasSimulationReuseDebugPreview = false;
    int16_t speedKmh = 0;
    char gearChar = 'N';
    int16_t engineRpm = 0;
    uint32_t frameId = 0u;
};

} // namespace GameLoopRuntime
