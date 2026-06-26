#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct SimulationSchedulerTelemetryViewPacket
{
    uint32_t slaveDispatchCount = 0u;
    uint32_t slaveDispatchSkipsTrackBusy = 0u;
    uint32_t slaveDispatchSkipsBackoff = 0u;
    uint32_t drainSoftTimeouts = 0u;
    uint32_t drainHardWaits = 0u;
    uint16_t masterWaitTicksThisFrame = 0u;
    uint16_t slaveLastJobTicksThisFrame = 0u;
    uint8_t slaveBackoffFrames = 0u;
    bool jobInFlight = false;
    bool hasCompleted = false;
};

} // namespace GameLoopRuntime
