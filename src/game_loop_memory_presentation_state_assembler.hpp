#pragma once

#include "game_loop_memory_presentation_contracts.hpp"
#include "game_loop_memory_trace_ops.hpp"

namespace GameLoopMemoryPresentationDomain
{

inline void SeedWorkRamUsagePacket(const MemoryBudgetDomain::MemorySnapshotPacket& snapshot,
                                   WorkRamUsagePacket& outPacket)
{
    outPacket.valid = true;
    outPacket.highWorkFree = snapshot.snapshot.highWorkFree;
    outPacket.lowWorkFree = snapshot.snapshot.lowWorkFree;
    outPacket.highWorkUsed =
        (snapshot.snapshot.highWorkTotal >= snapshot.snapshot.highWorkFree)
            ? (snapshot.snapshot.highWorkTotal - snapshot.snapshot.highWorkFree)
            : 0u;
    outPacket.lowWorkUsed =
        (snapshot.snapshot.lowWorkTotal >= snapshot.snapshot.lowWorkFree)
            ? (snapshot.snapshot.lowWorkTotal - snapshot.snapshot.lowWorkFree)
            : 0u;
}

inline void SeedLowWorkOverlayHeaderPacket(int32_t freeDelta,
                                           uint32_t lowWorkFree,
                                           uint32_t highWorkFree,
                                           uint8_t slides,
                                           int16_t slideId,
                                           LowWorkOverlayHeaderPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.freeDelta = freeDelta;
    outPacket.lowWorkFree = lowWorkFree;
    outPacket.highWorkFree = highWorkFree;
    outPacket.slides = slides;
    outPacket.slideId = slideId;
}

inline void SeedLowWorkOverlayTicksPacket(uint16_t trackStreamTicks,
                                          uint16_t trackMaintenanceTicks,
                                          uint16_t trackDrawTicks,
                                          uint16_t trackFrameTicks,
                                          uint16_t trackWindowTicks,
                                          uint16_t trackPrefetchTicks,
                                          uint16_t trackLodTicks,
                                          uint16_t trackWorkingSetTicks,
                                          uint8_t prefetchBuildAttempts,
                                          uint8_t prefetchBuildBudget,
                                          uint8_t prefetchBuildDrops,
                                          LowWorkOverlayTicksPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.trackStreamTicks = trackStreamTicks;
    outPacket.trackMaintenanceTicks = trackMaintenanceTicks;
    outPacket.trackDrawTicks = trackDrawTicks;
    outPacket.trackFrameTicks = trackFrameTicks;
    outPacket.trackWindowTicks = trackWindowTicks;
    outPacket.trackPrefetchTicks = trackPrefetchTicks;
    outPacket.trackLodTicks = trackLodTicks;
    outPacket.trackWorkingSetTicks = trackWorkingSetTicks;
    outPacket.prefetchBuildAttempts = prefetchBuildAttempts;
    outPacket.prefetchBuildBudget = prefetchBuildBudget;
    outPacket.prefetchBuildDrops = prefetchBuildDrops;
}

inline void SeedLowWorkOverlayPacket(const GameLoopRuntime::LowWorkOverlayState& overlayState,
                                     int32_t freeDelta,
                                     uint32_t lowWorkFree,
                                     uint32_t highWorkFree,
                                     uint8_t slides,
                                     int16_t slideId,
                                     LowWorkOverlayPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.freeDelta = freeDelta;
    outPacket.lowWorkFree = lowWorkFree;
    outPacket.highWorkFree = highWorkFree;
    outPacket.slides = slides;
    outPacket.slideId = slideId;
    outPacket.breakdown = overlayState.lastBreakdown;
    outPacket.tagGroups = overlayState.lastTagGroup;
    outPacket.payloadBytes = overlayState.lastPayloadBytes;
    outPacket.overheadBytes = overlayState.lastOverheadBytes;
    outPacket.freeBlocks = overlayState.lastFreeBlocks;
}

inline void AttachLowWorkTrackTicks(uint16_t trackStreamTicks,
                                    uint16_t trackMaintenanceTicks,
                                    uint16_t trackDrawTicks,
                                    uint16_t trackFrameTicks,
                                    uint16_t trackWindowTicks,
                                    uint16_t trackPrefetchTicks,
                                    uint16_t trackLodTicks,
                                    uint16_t trackWorkingSetTicks,
                                    uint8_t prefetchBuildAttempts,
                                    uint8_t prefetchBuildBudget,
                                    uint8_t prefetchBuildDrops,
                                    LowWorkOverlayPacket& ioPacket)
{
    ioPacket.trackStreamTicks = trackStreamTicks;
    ioPacket.trackMaintenanceTicks = trackMaintenanceTicks;
    ioPacket.trackDrawTicks = trackDrawTicks;
    ioPacket.trackFrameTicks = trackFrameTicks;
    ioPacket.trackWindowTicks = trackWindowTicks;
    ioPacket.trackPrefetchTicks = trackPrefetchTicks;
    ioPacket.trackLodTicks = trackLodTicks;
    ioPacket.trackWorkingSetTicks = trackWorkingSetTicks;
    ioPacket.prefetchBuildAttempts = prefetchBuildAttempts;
    ioPacket.prefetchBuildBudget = prefetchBuildBudget;
    ioPacket.prefetchBuildDrops = prefetchBuildDrops;
}

inline void AttachLowWorkAllocatorDiagnostics(uint32_t knownTaggedBytes,
                                              uint32_t invalidTaggedBytes,
                                              uint32_t invalidTaggedBlocks,
                                              LowWorkOverlayPacket& ioPacket)
{
    ioPacket.knownTaggedBytes = knownTaggedBytes;
    ioPacket.invalidTaggedBytes = invalidTaggedBytes;
    ioPacket.invalidTaggedBlocks = invalidTaggedBlocks;
}

inline void SeedHighWorkTracePacket(const GameLoopRuntime::HwrStageTrace& trace,
                                    HighWorkTracePacket& outPacket)
{
    outPacket.valid = true;
    outPacket.begin = trace.begin;
    outPacket.postSync = trace.postSync;
    outPacket.frameAccum = GameLoopRuntime::SnapshotLiveDelta(trace.begin, trace.postSync);
    outPacket.finishAccum = GameLoopRuntime::SnapshotLiveDelta(trace.car, trace.preSync);
    outPacket.syncAccum = GameLoopRuntime::SnapshotLiveDelta(trace.preSync, trace.postSync);
}

inline void SeedLowWorkTracePacket(const GameLoopRuntime::LwrStageTrace& trace,
                                   LowWorkTracePacket& outPacket)
{
    outPacket.valid = true;
    outPacket.begin = trace.begin;
    outPacket.gameplay = trace.gameplay;
    outPacket.autoLap = trace.autoLap;
    outPacket.background = trace.background;
    outPacket.hud = trace.hud;
    outPacket.trackDraw = trace.trackDraw;
    outPacket.trackEnd = trace.trackEnd;
    outPacket.car = trace.car;
    outPacket.preSync = trace.preSync;
    outPacket.postSync = trace.postSync;
}

} // namespace GameLoopMemoryPresentationDomain
