#pragma once

#include "game_loop_memory_trace_packet_assembler.hpp"
#include "game_loop_memory_trace_text_contracts.hpp"

namespace GameLoopMemoryPresentationDomain
{

inline void SeedHighWorkTraceTextPacket(const HighWorkTraceDeltaPacket& delta,
                                        HighWorkTraceTextPacket& outPacket)
{
    outPacket.valid = delta.valid;
    outPacket.allocDelta = delta.allocDelta;
    outPacket.freeDelta = delta.freeDelta;
    outPacket.reallocDelta = delta.reallocDelta;
    outPacket.failedDelta = delta.failedDelta;
    outPacket.usedBlocks = delta.usedBlocks;
    outPacket.freeBlocks = delta.freeBlocks;
    outPacket.finishAccum = delta.finishAccum;
    outPacket.syncAccum = delta.syncAccum;
    outPacket.freeBytes = delta.freeBytes;
}

inline HighWorkTraceTextPacket BuildHighWorkTraceTextPacket(const HighWorkTraceDeltaPacket& delta)
{
    HighWorkTraceTextPacket packet{};
    SeedHighWorkTraceTextPacket(delta, packet);
    return packet;
}

inline HighWorkTraceTextPacket BuildHighWorkTraceTextPacket(const HighWorkTracePacket& trace)
{
    return BuildHighWorkTraceTextPacket(BuildHighWorkTraceDeltaPacket(trace));
}

inline void SeedLowWorkTraceTextPacket(const LowWorkTraceDeltaPacket& delta,
                                       LowWorkTraceTextPacket& outPacket)
{
    outPacket.valid = delta.valid;
    outPacket.gameplayFreeDelta = delta.gameplayFreeDelta;
    outPacket.autoLapFreeDelta = delta.autoLapFreeDelta;
    outPacket.backgroundFreeDelta = delta.backgroundFreeDelta;
    outPacket.hudFreeDelta = delta.hudFreeDelta;
    outPacket.trackDrawFreeDelta = delta.trackDrawFreeDelta;
    outPacket.trackEndFreeDelta = delta.trackEndFreeDelta;
    outPacket.carFreeDelta = delta.carFreeDelta;
    outPacket.finishFreeDelta = delta.finishFreeDelta;
    outPacket.syncFreeDelta = delta.syncFreeDelta;
    outPacket.frameFreeDelta = delta.frameFreeDelta;
    outPacket.framePayloadDelta = delta.framePayloadDelta;
    outPacket.frameOverheadDelta = delta.frameOverheadDelta;
    outPacket.trackDrawPrepareDelta = delta.trackDrawPrepareDelta;
    outPacket.trackDrawExecuteDelta = delta.trackDrawExecuteDelta;
    outPacket.trackDrawOtherDelta = delta.trackDrawOtherDelta;
    outPacket.trackDrawFrameDelta = delta.trackDrawFrameDelta;
    outPacket.largestFreeDelta = delta.largestFreeDelta;
    outPacket.freeBlocksDelta = delta.freeBlocksDelta;
}

inline LowWorkTraceTextPacket BuildLowWorkTraceTextPacket(const LowWorkTraceDeltaPacket& delta)
{
    LowWorkTraceTextPacket packet{};
    SeedLowWorkTraceTextPacket(delta, packet);
    return packet;
}

inline LowWorkTraceTextPacket BuildLowWorkTraceTextPacket(const LowWorkTracePacket& trace,
                                                          const LowWorkTraceDeltaInputs& inputs)
{
    return BuildLowWorkTraceTextPacket(BuildLowWorkTraceDeltaPacket(trace, inputs));
}

} // namespace GameLoopMemoryPresentationDomain
