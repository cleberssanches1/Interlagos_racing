#pragma once

#include "game_loop_simulation_reuse_runtime_commit_assembler.hpp"
#include "simulation_frame_reuse_ops.hpp"

namespace GameLoopRuntime
{

inline void CommitAuthoritativeSimulationOutputForReuse(
    const Game::SimulationPayload& authoritativeOutput,
    FrameReuseDomain::SimulationFrameHistoryState& ioHistory)
{
    FrameReuseDomain::CommitSimulationFramePacket(authoritativeOutput.frameState,
                                                  ioHistory);
}

inline void CommitAuthoritativeSimulationOutputForReuse(
    const SimulationReuseRuntimeCommitPacket& commitPacket,
    FrameReuseDomain::SimulationFrameHistoryState& ioHistory)
{
    if (!commitPacket.valid)
    {
        return;
    }

    CommitAuthoritativeSimulationOutputForReuse(commitPacket.authoritativeOutput,
                                                ioHistory);
}

} // namespace GameLoopRuntime
