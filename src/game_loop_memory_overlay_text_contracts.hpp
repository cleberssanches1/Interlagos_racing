#pragma once

#include <cstdint>

namespace GameLoopMemoryPresentationDomain
{

struct LowWorkOverlayHeaderTextPacket
{
    bool valid = false;
    uint32_t lowWorkFree = 0u;
    int32_t freeDelta = 0;
    uint8_t slides = 0u;
    int16_t slideId = -1;
};

struct HighWorkOverlayTextPacket
{
    bool valid = false;
    uint32_t initUnknown = 0u;
    uint32_t gameplayAuto = 0u;
    uint32_t ui = 0u;
    uint32_t track = 0u;
    uint32_t car = 0u;
    uint32_t finishSync = 0u;
    uint32_t freeBytes = 0u;
};

struct LowWorkOverlayBreakdownTextPacket
{
    bool valid = false;
    uint32_t renderers = 0u;
    uint32_t slotState = 0u;
    uint32_t workingSet = 0u;
    uint32_t familyCache = 0u;
    uint32_t transient = 0u;
    uint32_t metadata = 0u;
};

struct LowWorkOverlayTicksTextPacket
{
    bool valid = false;
    uint16_t trackStreamTicks = 0u;
    uint16_t trackMaintenanceTicks = 0u;
    uint16_t trackDrawTicks = 0u;
    uint16_t trackFrameTicks = 0u;
    uint16_t trackWindowTicks = 0u;
    uint16_t trackPrefetchTicks = 0u;
    uint16_t trackLodTicks = 0u;
    uint16_t trackWorkingSetTicks = 0u;
    uint8_t prefetchBuildAttempts = 0u;
    uint8_t prefetchBuildBudget = 0u;
    uint8_t prefetchBuildDrops = 0u;
};

struct LowWorkTagGroupTextPacket
{
    bool valid = false;
    uint32_t initUnknown = 0u;
    uint32_t gameplayAuto = 0u;
    uint32_t ui = 0u;
    uint32_t track = 0u;
    uint32_t car = 0u;
    uint32_t finishSync = 0u;
};

struct LowWorkAllocatorTextPacket
{
    bool valid = false;
    uint32_t payloadBytes = 0u;
    uint32_t overheadBytes = 0u;
    uint32_t freeBlocks = 0u;
    uint32_t knownTaggedBytes = 0u;
    uint32_t invalidTaggedBytes = 0u;
    uint32_t invalidTaggedBlocks = 0u;
};

struct LowWorkOverlayTextBundle
{
    bool valid = false;
    LowWorkOverlayHeaderTextPacket header{};
    HighWorkOverlayTextPacket highWork{};
    LowWorkOverlayBreakdownTextPacket breakdown{};
    LowWorkOverlayTicksTextPacket ticks{};
    LowWorkTagGroupTextPacket tagGroups{};
    LowWorkAllocatorTextPacket allocator{};
};

} // namespace GameLoopMemoryPresentationDomain
