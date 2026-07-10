#pragma once

namespace GameLoopRuntime
{

struct PresenterPresenceDecisionPacket
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

} // namespace GameLoopRuntime
