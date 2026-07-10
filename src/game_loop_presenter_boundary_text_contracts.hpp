#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct PresenterBoundaryStatusTextPacket
{
    bool valid = false;
    char gearChar = 'N';
    int16_t speedKmh = 0;
    int16_t engineRpm = 0;
    bool hasObservability = false;
    bool hasSchedulerReuseDebug = false;
    uint32_t frameId = 0u;
};

struct PresenterBoundaryDecisionTextPacket
{
    bool valid = false;
    bool shouldPresentDrivingHud = false;
    bool shouldPresentPeriodicHud = false;
    bool shouldPresentRenderDebug = false;
    bool shouldPresentOverlayDebug = false;
    bool shouldPresentObservability = false;
    bool shouldPresentSchedulerReuseDebug = false;
    bool shouldPresentMemoryDebug = false;
};

struct PresenterBoundaryTextPacket
{
    bool valid = false;
    PresenterBoundaryStatusTextPacket status{};
    PresenterBoundaryDecisionTextPacket decision{};
};

} // namespace GameLoopRuntime
