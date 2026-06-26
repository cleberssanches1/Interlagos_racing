#pragma once

#include "game_loop_memory_overlay_text_contracts.hpp"
#include "game_loop_memory_overlay_text_view_contracts.hpp"

namespace GameLoopMemoryPresentationDomain
{

inline void SeedLowWorkOverlayTextViewPacket(const LowWorkOverlayTextBundle& bundle,
                                             LowWorkOverlayTextViewPacket& outPacket)
{
    outPacket.valid = bundle.valid;
    outPacket.shouldShowHeader = bundle.header.valid;
    outPacket.shouldShowHighWorkSummary = bundle.highWork.valid;
    outPacket.shouldShowBreakdown = bundle.breakdown.valid;
    outPacket.shouldShowTicks = bundle.ticks.valid;
    outPacket.shouldShowTagGroups = bundle.tagGroups.valid;
    outPacket.shouldShowAllocator = bundle.allocator.valid;
    outPacket.lowWorkFree = bundle.header.lowWorkFree;
    outPacket.freeDelta = bundle.header.freeDelta;
    outPacket.slides = bundle.header.slides;
    outPacket.slideId = bundle.header.slideId;
    outPacket.highWorkFreeBytes = bundle.highWork.freeBytes;
    outPacket.trackFrameTicks = bundle.ticks.trackFrameTicks;
    outPacket.prefetchBuildAttempts = bundle.ticks.prefetchBuildAttempts;
    outPacket.prefetchBuildBudget = bundle.ticks.prefetchBuildBudget;
    outPacket.prefetchBuildDrops = bundle.ticks.prefetchBuildDrops;
    outPacket.payloadBytes = bundle.allocator.payloadBytes;
    outPacket.overheadBytes = bundle.allocator.overheadBytes;
    outPacket.invalidTaggedBlocks = bundle.allocator.invalidTaggedBlocks;
}

inline LowWorkOverlayTextViewPacket BuildLowWorkOverlayTextViewPacket(
    const LowWorkOverlayTextBundle& bundle)
{
    LowWorkOverlayTextViewPacket packet{};
    SeedLowWorkOverlayTextViewPacket(bundle, packet);
    return packet;
}

} // namespace GameLoopMemoryPresentationDomain
