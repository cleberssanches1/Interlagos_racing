#pragma once

#include "game_loop_simulation_reuse_runtime_debug_preview_contracts.hpp"
#include "game_loop_simulation_reuse_runtime_preview_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedSimulationReuseRuntimeDebugPreviewPacket(
    const GameLoopRuntime::SimulationReuseRuntimePreviewPacket& preview,
    SimulationReuseRuntimeDebugPreviewPacket& outPacket)
{
    outPacket.valid = preview.valid;
    outPacket.preview = preview;
}

inline SimulationReuseRuntimeDebugPreviewPacket BuildSimulationReuseRuntimeDebugPreviewPacket(
    const GameLoopRuntime::SimulationReuseRuntimePreviewPacket& preview)
{
    SimulationReuseRuntimeDebugPreviewPacket packet{};
    SeedSimulationReuseRuntimeDebugPreviewPacket(preview, packet);
    return packet;
}

inline SimulationReuseRuntimeDebugPreviewPacket BuildSimulationReuseRuntimeDebugPreviewPacket(
    const GameLoopRuntime::SimulationReuseRuntimeDecisionInputsPacket& inputs)
{
    return BuildSimulationReuseRuntimeDebugPreviewPacket(
        GameLoopRuntime::BuildSimulationReuseRuntimePreviewPacket(inputs));
}

inline SimulationReuseRuntimeDebugPreviewPacket BuildSimulationReuseRuntimeDebugPreviewPacket(
    uint32_t requestFrameId,
    bool slaveSimulationEnabled,
    bool lockstepEnabled,
    bool jobInFlight,
    const FrameReuseDomain::SimulationFrameHistoryState& history)
{
    return BuildSimulationReuseRuntimeDebugPreviewPacket(
        GameLoopRuntime::BuildSimulationReuseRuntimePreviewPacket(requestFrameId,
                                                                 slaveSimulationEnabled,
                                                                 lockstepEnabled,
                                                                 jobInFlight,
                                                                 history));
}

} // namespace GameLoopObservabilityDomain
