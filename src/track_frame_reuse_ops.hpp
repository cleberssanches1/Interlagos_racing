#pragma once

#include "frame_reuse_contracts.hpp"

namespace FrameReuseDomain
{

inline void ResetTrackFrameHistory(TrackFrameHistoryState& ioState)
{
    ioState = TrackFrameHistoryState{};
}

inline void CommitTrackFramePacket(uint32_t frameId,
                                   int16_t activeSegmentId,
                                   bool valid,
                                   TrackFrameHistoryState& ioState)
{
    const uint8_t slot = ioState.writeIdx;
    Game::TrackRenderPacket& packet = ioState.packets[slot];
    packet.frameId = frameId;
    packet.activeSegmentId = activeSegmentId;
    packet.valid = valid;
    ioState.committedIdx = slot;
    ioState.writeIdx ^= 1u;
    ioState.hasCommittedPacket = valid;
}

inline void SeedTrackReuseDecisionPacket(uint32_t requestFrameId,
                                         int16_t activeSegmentId,
                                         bool renderEnabled,
                                         bool lockstepEnabled,
                                         bool producerJobInFlight,
                                         const TrackFrameHistoryState& history,
                                         TrackReuseDecisionPacket& outPacket)
{
    outPacket.valid = renderEnabled;
    outPacket.renderEnabled = renderEnabled;
    outPacket.mode = lockstepEnabled ? ReuseMode::Lockstep : ReuseMode::PreviousFrame;
    outPacket.requestFrameId = requestFrameId;
    outPacket.activeSegmentId = activeSegmentId;
    outPacket.dispatchSlot = history.writeIdx;
    outPacket.consumeSlot = history.committedIdx;

    if (!renderEnabled)
    {
        return;
    }

    if (!history.hasCommittedPacket)
    {
        outPacket.shouldKickProducer = !producerJobInFlight;
        outPacket.requiresLockstepWait = lockstepEnabled && producerJobInFlight;
        outPacket.requiresSynchronousFallback =
            !lockstepEnabled && producerJobInFlight;
        return;
    }

    const Game::TrackRenderPacket& packet = history.packets[history.committedIdx];
    outPacket.hasCommittedPacket = packet.valid;
    outPacket.committedFrameId = packet.frameId;
    outPacket.hasExactFrameCandidate = packet.valid && (packet.frameId == requestFrameId);
    outPacket.hasPreviousFrameCandidate = packet.valid && (packet.frameId < requestFrameId);
    outPacket.shouldConsumeCommitted =
        lockstepEnabled ? outPacket.hasExactFrameCandidate
                        : outPacket.hasPreviousFrameCandidate;
    outPacket.shouldKickProducer = !producerJobInFlight;
    outPacket.requiresLockstepWait =
        lockstepEnabled && producerJobInFlight && !outPacket.hasExactFrameCandidate;
    outPacket.requiresSynchronousFallback =
        !lockstepEnabled &&
        !outPacket.shouldConsumeCommitted &&
        producerJobInFlight;
}

inline bool CanConsumeTrackReusePacket(const TrackReuseDecisionPacket& packet)
{
    return packet.valid && packet.shouldConsumeCommitted;
}

inline void RecordTrackReuseTelemetry(const TrackReuseDecisionPacket& packet,
                                      FrameReuseTelemetry& ioTelemetry)
{
    if (packet.hasCommittedPacket && packet.committedFrameId > 0u)
    {
        ioTelemetry.trackPacketsCommitted =
            ioTelemetry.trackPacketsCommitted + 1u;
    }
    if (packet.shouldConsumeCommitted)
    {
        if (packet.mode == ReuseMode::Lockstep)
        {
            ioTelemetry.trackLockstepConsumes =
                ioTelemetry.trackLockstepConsumes + 1u;
        }
        else
        {
            ioTelemetry.trackPreviousFrameConsumes =
                ioTelemetry.trackPreviousFrameConsumes + 1u;
        }
    }
    if (packet.requiresSynchronousFallback)
    {
        ioTelemetry.trackFallbacks = ioTelemetry.trackFallbacks + 1u;
    }
}

} // namespace FrameReuseDomain
