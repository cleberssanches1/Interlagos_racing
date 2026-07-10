#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct PresenterSummarySchedulerReuseSimulationStatusTextPacket
{
    bool valid = false;
    char gearChar = 'N';
    int16_t speedKmh = 0;
    int16_t engineRpm = 0;
    bool hasObservability = false;
    bool hasSchedulerReuseDebug = false;
    bool hasSimulationReuseDebugPreview = false;
    uint32_t frameId = 0u;
};

struct PresenterSummarySchedulerReuseSimulationTextPacket
{
    bool valid = false;
    PresenterSummarySchedulerReuseSimulationStatusTextPacket status{};
};

} // namespace GameLoopRuntime
