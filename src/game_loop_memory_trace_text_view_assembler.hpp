#pragma once

#include "game_loop_memory_trace_text_contracts.hpp"
#include "game_loop_memory_trace_text_view_contracts.hpp"

namespace GameLoopMemoryPresentationDomain
{

inline void SeedHighWorkTraceTextViewPacket(const HighWorkTraceTextPacket& packet,
                                            HighWorkTraceTextViewPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.shouldShowCallDeltas =
        packet.allocDelta > 0u || packet.freeDelta > 0u || packet.reallocDelta > 0u || packet.failedDelta > 0u;
    outPacket.shouldShowBlockSummary = packet.usedBlocks > 0u || packet.freeBlocks > 0u;
    outPacket.shouldShowSyncSummary = packet.finishAccum != 0 || packet.syncAccum != 0 || packet.freeBytes > 0u;
    outPacket.allocDelta = packet.allocDelta;
    outPacket.freeDelta = packet.freeDelta;
    outPacket.reallocDelta = packet.reallocDelta;
    outPacket.failedDelta = packet.failedDelta;
    outPacket.usedBlocks = packet.usedBlocks;
    outPacket.freeBlocks = packet.freeBlocks;
    outPacket.finishAccum = packet.finishAccum;
    outPacket.syncAccum = packet.syncAccum;
    outPacket.freeBytes = packet.freeBytes;
}

inline HighWorkTraceTextViewPacket BuildHighWorkTraceTextViewPacket(const HighWorkTraceTextPacket& packet)
{
    HighWorkTraceTextViewPacket outPacket{};
    SeedHighWorkTraceTextViewPacket(packet, outPacket);
    return outPacket;
}

} // namespace GameLoopMemoryPresentationDomain
