#pragma once

#include "frame_reuse_contracts.hpp"

namespace FrameReuseDomain
{

inline void ResetSimulationFrameHistory(SimulationFrameHistoryState& ioState)
{
    ioState = SimulationFrameHistoryState{};
}

inline void CommitSimulationFramePacket(const Game::GameplayFrameState& frameState,
                                        SimulationFrameHistoryState& ioState)
{
    const uint8_t slot = ioState.writeIdx;
    Game::SimulationFramePacket& packet = ioState.packets[slot];
    packet.frameId = frameState.frameId;
    packet.gameplayFrame = frameState;
    packet.valid = true;
    ioState.committedIdx = slot;
    ioState.writeIdx ^= 1u;
    ioState.hasCommittedPacket = true;
}

inline void SeedSimulationReuseDecisionPacket(uint32_t requestFrameId,
                                              bool slaveSimulationEnabled,
                                              bool lockstepEnabled,
                                              bool jobInFlight,
                                              const SimulationFrameHistoryState& history,
                                              SimulationReuseDecisionPacket& outPacket)
{
    outPacket.valid = slaveSimulationEnabled;
    outPacket.mode = lockstepEnabled ? ReuseMode::Lockstep : ReuseMode::PreviousFrame;
    outPacket.requestFrameId = requestFrameId;
    outPacket.dispatchSlot = history.writeIdx;
    outPacket.consumeSlot = history.committedIdx;

    if (!history.hasCommittedPacket)
    {
        outPacket.shouldDispatchNextFrame = slaveSimulationEnabled && !jobInFlight;
        outPacket.requiresLockstepWait = lockstepEnabled && jobInFlight;
        outPacket.requiresSynchronousFallback =
            !lockstepEnabled && !outPacket.shouldDispatchNextFrame;
        return;
    }

    const Game::SimulationFramePacket& packet = history.packets[history.committedIdx];
    outPacket.hasCommittedPacket = packet.valid;
    outPacket.committedFrameId = packet.frameId;
    outPacket.hasExactFrameCandidate = packet.valid && (packet.frameId == requestFrameId);
    outPacket.hasPreviousFrameCandidate = packet.valid && (packet.frameId < requestFrameId);
    outPacket.shouldConsumeCommitted =
        lockstepEnabled ? outPacket.hasExactFrameCandidate
                        : outPacket.hasPreviousFrameCandidate;
    outPacket.shouldDispatchNextFrame = slaveSimulationEnabled && !jobInFlight;
    outPacket.requiresLockstepWait =
        lockstepEnabled && jobInFlight && !outPacket.hasExactFrameCandidate;
    outPacket.requiresSynchronousFallback =
        !lockstepEnabled &&
        !outPacket.shouldConsumeCommitted &&
        !outPacket.shouldDispatchNextFrame;
}

inline bool CanConsumeSimulationReusePacket(const SimulationReuseDecisionPacket& packet)
{
    return packet.valid && packet.shouldConsumeCommitted;
}

inline void RecordSimulationReuseTelemetry(const SimulationReuseDecisionPacket& packet,
                                           FrameReuseTelemetry& ioTelemetry)
{
    if (packet.hasCommittedPacket && packet.committedFrameId > 0u)
    {
        ioTelemetry.simulationPacketsCommitted =
            ioTelemetry.simulationPacketsCommitted + 1u;
    }
    if (packet.shouldConsumeCommitted)
    {
        if (packet.mode == ReuseMode::Lockstep)
        {
            ioTelemetry.simulationLockstepConsumes =
                ioTelemetry.simulationLockstepConsumes + 1u;
        }
        else
        {
            ioTelemetry.simulationPreviousFrameConsumes =
                ioTelemetry.simulationPreviousFrameConsumes + 1u;
        }
    }
    if (packet.requiresSynchronousFallback)
    {
        ioTelemetry.simulationFallbacks = ioTelemetry.simulationFallbacks + 1u;
    }
}

} // namespace FrameReuseDomain
