#pragma once

#include "frame_reuse_contracts.hpp"

namespace GameLoopRuntime
{

struct SimulationReuseRuntimeDecisionInputsPacket
{
    bool valid = false;
    uint32_t requestFrameId = 0u;
    bool slaveSimulationEnabled = false;
    bool lockstepEnabled = false;
    bool jobInFlight = false;
    FrameReuseDomain::SimulationFrameHistoryState history{};
};

} // namespace GameLoopRuntime
