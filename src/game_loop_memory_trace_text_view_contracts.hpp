#pragma once

#include <cstdint>

namespace GameLoopMemoryPresentationDomain
{

struct HighWorkTraceTextViewPacket
{
    bool valid = false;
    bool shouldShowCallDeltas = false;
    bool shouldShowBlockSummary = false;
    bool shouldShowSyncSummary = false;
    uint32_t allocDelta = 0u;
    uint32_t freeDelta = 0u;
    uint32_t reallocDelta = 0u;
    uint32_t failedDelta = 0u;
    uint32_t usedBlocks = 0u;
    uint32_t freeBlocks = 0u;
    int32_t finishAccum = 0;
    int32_t syncAccum = 0;
    uint32_t freeBytes = 0u;
};

} // namespace GameLoopMemoryPresentationDomain
