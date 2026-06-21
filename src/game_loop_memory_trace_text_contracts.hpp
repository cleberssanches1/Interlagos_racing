#pragma once

#include <cstdint>

namespace GameLoopMemoryPresentationDomain
{

struct HighWorkTraceTextPacket
{
    bool valid = false;
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

struct LowWorkTraceTextPacket
{
    bool valid = false;
    int32_t gameplayFreeDelta = 0;
    int32_t autoLapFreeDelta = 0;
    int32_t backgroundFreeDelta = 0;
    int32_t hudFreeDelta = 0;
    int32_t trackDrawFreeDelta = 0;
    int32_t trackEndFreeDelta = 0;
    int32_t carFreeDelta = 0;
    int32_t finishFreeDelta = 0;
    int32_t syncFreeDelta = 0;
    int32_t frameFreeDelta = 0;
    int32_t framePayloadDelta = 0;
    int32_t frameOverheadDelta = 0;
    int32_t trackDrawPrepareDelta = 0;
    int32_t trackDrawExecuteDelta = 0;
    int32_t trackDrawOtherDelta = 0;
    int32_t trackDrawFrameDelta = 0;
    int32_t largestFreeDelta = 0;
    int32_t freeBlocksDelta = 0;
};

} // namespace GameLoopMemoryPresentationDomain
