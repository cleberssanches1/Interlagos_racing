#pragma once

#include "game_loop_simulation_reuse_runtime_commit_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedSimulationReuseRuntimeCommitPacket(
    const Game::SimulationPayload& authoritativeOutput,
    SimulationReuseRuntimeCommitPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.authoritativeOutput = authoritativeOutput;
}

inline SimulationReuseRuntimeCommitPacket BuildSimulationReuseRuntimeCommitPacket(
    const Game::SimulationPayload& authoritativeOutput)
{
    SimulationReuseRuntimeCommitPacket packet{};
    SeedSimulationReuseRuntimeCommitPacket(authoritativeOutput, packet);
    return packet;
}

} // namespace GameLoopRuntime
