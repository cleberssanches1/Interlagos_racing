#pragma once

#include "frame_reuse_observability_capture_ops.hpp"

#include "game_loop_simulation_reuse_runtime_commit_ops.hpp"
#include "game_loop_simulation_reuse_runtime_state_contracts.hpp"

namespace GameLoopRuntime
{

inline void ResetSimulationReuseRuntimeState(SimulationReuseRuntimeState& ioState)
{
    ioState = SimulationReuseRuntimeState{};
}

inline void CommitSimulationReuseRuntimeFrame(
    const Game::SimulationPayload& authoritativeOutput,
    SimulationReuseRuntimeState& ioState)
{
    CommitAuthoritativeSimulationOutputForReuse(authoritativeOutput,
                                                ioState.history);
}

inline void CommitSimulationReuseRuntimeFrame(
    const SimulationReuseRuntimeCommitPacket& commitPacket,
    SimulationReuseRuntimeState& ioState)
{
    CommitAuthoritativeSimulationOutputForReuse(commitPacket, ioState.history);
}

inline FrameReuseDomain::FrameReuseRuntimeOwnerPacket
BuildSimulationReuseRuntimeOwnerPacket(
    const SimulationReuseRuntimeState& state,
    const FrameReuseDomain::SimulationReuseDecisionPacket* simulationDecision =
        nullptr,
    const FrameReuseDomain::FrameReuseTelemetry* telemetry = nullptr)
{
    return FrameReuseDomain::CaptureFrameReuseRuntimeOwnerPacket(&state.history,
                                                                 nullptr,
                                                                 simulationDecision,
                                                                 nullptr,
                                                                 telemetry);
}

} // namespace GameLoopRuntime
