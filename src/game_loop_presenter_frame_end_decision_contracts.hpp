#pragma once

namespace GameLoopRuntime
{

struct PresenterFrameEndDecisionPacket
{
    bool valid = false;
    bool shouldPresentDrivingHud = false;
    bool shouldPresentPeriodicHud = false;
    bool shouldPresentOverlayDebug = false;
    bool shouldPresentMemoryDebug = false;
};

} // namespace GameLoopRuntime
