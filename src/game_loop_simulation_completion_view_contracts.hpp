#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct SimulationCompletionViewPacket
{
    bool valid = false;
    bool jobInFlight = false;
    bool hasCompleted = false;
    uint8_t inFlightIdx = 0u;
    uint8_t completedIdx = 0u;
};

} // namespace GameLoopRuntime
