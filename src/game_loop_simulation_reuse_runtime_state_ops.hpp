#pragma once

#include "frame_reuse_observability_capture_ops.hpp"

#include "game_loop_simulation_reuse_runtime_commit_ops.hpp"
#include "game_loop_simulation_reuse_runtime_state_contracts.hpp"
#include "simulation_scheduler_contracts.hpp"
#include "simulation_scheduler_state.hpp"

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

inline const Game::SimulationPayload&
CommitCompletedSimulationReuseAuthoritativeOutput(
    const Game::SimulationRuntimeState& runtimeState,
    SimulationReuseRuntimeState& ioState)
{
    const Game::SimulationPayload& authoritativeOutput =
        runtimeState.output[runtimeState.completedIdx];
    CommitSimulationReuseRuntimeFrame(authoritativeOutput, ioState);
    return authoritativeOutput;
}

inline const Game::SimulationPayload*
TryCommitCompletedSimulationReuseAuthoritativeOutput(
    const Game::SimulationRuntimeState& runtimeState,
    const SimulationSchedulerDomain::SimulationCompletionPacket& completionPacket,
    SimulationReuseRuntimeState& ioState)
{
    if (!completionPacket.hasCompleted)
    {
        return nullptr;
    }

    const Game::SimulationPayload& authoritativeOutput =
        runtimeState.output[completionPacket.completedIdx];
    CommitSimulationReuseRuntimeFrame(authoritativeOutput, ioState);
    return &authoritativeOutput;
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
