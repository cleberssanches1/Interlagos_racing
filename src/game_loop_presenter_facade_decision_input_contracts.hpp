#pragma once

namespace GameLoopRuntime
{

struct PresenterFacadeDecisionInputPacket
{
    bool valid = false;
    bool hasDrivingHud = false;
    bool hasPeriodicHud = false;
    bool hasOverlayDebug = false;
    bool hasMemoryDebug = false;
};

} // namespace GameLoopRuntime
