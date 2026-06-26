#pragma once

#include "game_loop_memory_trace_text_contracts.hpp"
#include "game_loop_memory_trace_text_low_work_view_contracts.hpp"

namespace GameLoopMemoryPresentationDomain
{

inline void SeedLowWorkTraceTextViewPacket(const LowWorkTraceTextPacket& packet,
                                           LowWorkTraceTextViewPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.shouldShowStageDeltasA =
        packet.gameplayFreeDelta != 0 || packet.autoLapFreeDelta != 0 ||
        packet.backgroundFreeDelta != 0 || packet.hudFreeDelta != 0;
    outPacket.shouldShowStageDeltasB =
        packet.trackDrawFreeDelta != 0 || packet.trackEndFreeDelta != 0 ||
        packet.carFreeDelta != 0 || packet.finishFreeDelta != 0;
    outPacket.shouldShowFrameDeltas =
        packet.syncFreeDelta != 0 || packet.frameFreeDelta != 0 ||
        packet.framePayloadDelta != 0 || packet.frameOverheadDelta != 0;
    outPacket.shouldShowTrackDrawDeltas =
        packet.trackDrawPrepareDelta != 0 || packet.trackDrawExecuteDelta != 0 ||
        packet.trackDrawOtherDelta != 0 || packet.trackDrawFrameDelta != 0;
    outPacket.shouldShowAllocatorDeltas =
        packet.largestFreeDelta != 0 || packet.freeBlocksDelta != 0;
    outPacket.gameplayFreeDelta = packet.gameplayFreeDelta;
    outPacket.autoLapFreeDelta = packet.autoLapFreeDelta;
    outPacket.backgroundFreeDelta = packet.backgroundFreeDelta;
    outPacket.hudFreeDelta = packet.hudFreeDelta;
    outPacket.trackDrawFreeDelta = packet.trackDrawFreeDelta;
    outPacket.trackEndFreeDelta = packet.trackEndFreeDelta;
    outPacket.carFreeDelta = packet.carFreeDelta;
    outPacket.finishFreeDelta = packet.finishFreeDelta;
    outPacket.syncFreeDelta = packet.syncFreeDelta;
    outPacket.frameFreeDelta = packet.frameFreeDelta;
    outPacket.framePayloadDelta = packet.framePayloadDelta;
    outPacket.frameOverheadDelta = packet.frameOverheadDelta;
    outPacket.trackDrawPrepareDelta = packet.trackDrawPrepareDelta;
    outPacket.trackDrawExecuteDelta = packet.trackDrawExecuteDelta;
    outPacket.trackDrawOtherDelta = packet.trackDrawOtherDelta;
    outPacket.trackDrawFrameDelta = packet.trackDrawFrameDelta;
    outPacket.largestFreeDelta = packet.largestFreeDelta;
    outPacket.freeBlocksDelta = packet.freeBlocksDelta;
}

inline LowWorkTraceTextViewPacket BuildLowWorkTraceTextViewPacket(const LowWorkTraceTextPacket& packet)
{
    LowWorkTraceTextViewPacket outPacket{};
    SeedLowWorkTraceTextViewPacket(packet, outPacket);
    return outPacket;
}

} // namespace GameLoopMemoryPresentationDomain
