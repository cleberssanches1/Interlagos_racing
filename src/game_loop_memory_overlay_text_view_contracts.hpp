#pragma once

#include <cstdint>

namespace GameLoopMemoryPresentationDomain
{

struct LowWorkOverlayTextViewPacket
{
    bool valid = false;
    bool shouldShowHeader = false;
    bool shouldShowHighWorkSummary = false;
    bool shouldShowBreakdown = false;
    bool shouldShowTicks = false;
    bool shouldShowTagGroups = false;
    bool shouldShowAllocator = false;
    uint32_t lowWorkFree = 0u;
    int32_t freeDelta = 0;
    uint8_t slides = 0u;
    int16_t slideId = -1;
    uint32_t highWorkFreeBytes = 0u;
    uint16_t trackFrameTicks = 0u;
    uint8_t prefetchBuildAttempts = 0u;
    uint8_t prefetchBuildBudget = 0u;
    uint8_t prefetchBuildDrops = 0u;
    uint32_t payloadBytes = 0u;
    uint32_t overheadBytes = 0u;
    uint32_t invalidTaggedBlocks = 0u;
};

} // namespace GameLoopMemoryPresentationDomain
