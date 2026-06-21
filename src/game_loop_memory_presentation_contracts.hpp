#pragma once

#include <cstdint>

#include "game_loop_debug_state.hpp"
#include "memory_budget_contracts.hpp"

namespace GameLoopMemoryPresentationDomain
{

enum class Stage : uint8_t
{
    WorkRamUsage = 0,
    LowWorkOverlay,
    HighWorkTrace,
    LowWorkTrace
};

struct WorkRamUsagePacket
{
    bool valid = false;
    uint32_t highWorkUsed = 0u;
    uint32_t highWorkFree = 0u;
    uint32_t lowWorkUsed = 0u;
    uint32_t lowWorkFree = 0u;
};

struct LowWorkOverlayHeaderPacket
{
    bool valid = false;
    int32_t freeDelta = 0;
    uint32_t lowWorkFree = 0u;
    uint32_t highWorkFree = 0u;
    uint8_t slides = 0u;
    int16_t slideId = -1;
};

struct LowWorkOverlayTicksPacket
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

struct LowWorkOverlayBreakdownPacket
{
    bool valid = false;
    uint32_t renderers = 0u;
    uint32_t slotState = 0u;
    uint32_t workingSet = 0u;
    uint32_t familyCache = 0u;
    uint32_t transient = 0u;
    uint32_t metadata = 0u;
};

struct HighWorkOverlayPacket
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

struct LowWorkTagGroupPacket
{
    bool valid = false;
    uint32_t initUnknown = 0u;
    uint32_t gameplayAuto = 0u;
    uint32_t ui = 0u;
    uint32_t track = 0u;
    uint32_t car = 0u;
    uint32_t finishSync = 0u;
};

struct LowWorkAllocatorPacket
{
    bool valid = false;
    uint32_t payloadBytes = 0u;
    uint32_t overheadBytes = 0u;
    uint32_t freeBlocks = 0u;
    uint32_t knownTaggedBytes = 0u;
    uint32_t invalidTaggedBytes = 0u;
    uint32_t invalidTaggedBlocks = 0u;
};

struct LowWorkOverlayPacket
{
    bool valid = false;
    int32_t freeDelta = 0;
    uint32_t lowWorkFree = 0u;
    uint32_t highWorkFree = 0u;
    uint8_t slides = 0u;
    int16_t slideId = -1;
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
    TrackSystem::LowWorkCategoryBreakdown breakdown{};
    GameLoopRuntime::LowWorkTagGroupOverlay tagGroups{};
    uint32_t payloadBytes = 0u;
    uint32_t overheadBytes = 0u;
    uint32_t freeBlocks = 0u;
    uint32_t knownTaggedBytes = 0u;
    uint32_t invalidTaggedBytes = 0u;
    uint32_t invalidTaggedBlocks = 0u;
};

struct HighWorkTracePacket
{
    bool valid = false;
    GameLoopRuntime::HwrStageTrace::Snapshot begin{};
    GameLoopRuntime::HwrStageTrace::Snapshot postSync{};
    int32_t frameAccum = 0;
    int32_t finishAccum = 0;
    int32_t syncAccum = 0;
};

struct HighWorkTraceDeltaPacket
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

struct LowWorkTracePacket
{
    bool valid = false;
    GameLoopRuntime::LwrStageTrace::Snapshot begin{};
    GameLoopRuntime::LwrStageTrace::Snapshot gameplay{};
    GameLoopRuntime::LwrStageTrace::Snapshot autoLap{};
    GameLoopRuntime::LwrStageTrace::Snapshot background{};
    GameLoopRuntime::LwrStageTrace::Snapshot hud{};
    GameLoopRuntime::LwrStageTrace::Snapshot trackDraw{};
    GameLoopRuntime::LwrStageTrace::Snapshot trackEnd{};
    GameLoopRuntime::LwrStageTrace::Snapshot car{};
    GameLoopRuntime::LwrStageTrace::Snapshot preSync{};
    GameLoopRuntime::LwrStageTrace::Snapshot postSync{};
};

struct LowWorkTraceDeltaPacket
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
    int32_t largestFreeDelta = 0;
    int32_t freeBlocksDelta = 0;
    int32_t trackDrawPrepareDelta = 0;
    int32_t trackDrawExecuteDelta = 0;
    int32_t trackDrawOtherDelta = 0;
    int32_t trackDrawFrameDelta = 0;
};

} // namespace GameLoopMemoryPresentationDomain
