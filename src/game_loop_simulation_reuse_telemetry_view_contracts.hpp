#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct SimulationReuseTelemetryViewPacket
{
    uint32_t simulationPacketsCommitted = 0u;
    uint32_t simulationPreviousFrameConsumes = 0u;
    uint32_t simulationLockstepConsumes = 0u;
    uint32_t simulationFallbacks = 0u;
};

} // namespace GameLoopRuntime
