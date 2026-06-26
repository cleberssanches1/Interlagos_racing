#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct SimulationDrainViewPacket
{
    bool valid = false;
    bool mandatoryWait = false;
    uint32_t softSpinLimit = 0u;
    uint32_t hardSpinLimit = 0u;
};

} // namespace GameLoopRuntime
