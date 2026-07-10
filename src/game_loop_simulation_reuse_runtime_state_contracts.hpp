#pragma once

#include "frame_reuse_contracts.hpp"

namespace GameLoopRuntime
{

struct SimulationReuseRuntimeState
{
    FrameReuseDomain::SimulationFrameHistoryState history{};
};

} // namespace GameLoopRuntime
