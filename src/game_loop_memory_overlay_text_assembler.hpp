#pragma once

#include "game_loop_memory_overlay_text_contracts.hpp"
#include "game_loop_memory_presentation_contracts.hpp"

namespace GameLoopMemoryPresentationDomain
{

inline void SeedLowWorkOverlayHeaderTextPacket(const LowWorkOverlayHeaderPacket& packet,
                                               LowWorkOverlayHeaderTextPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.lowWorkFree = packet.lowWorkFree;
    outPacket.freeDelta = packet.freeDelta;
    outPacket.slides = packet.slides;
    outPacket.slideId = packet.slideId;
}

inline LowWorkOverlayHeaderTextPacket BuildLowWorkOverlayHeaderTextPacket(
    const LowWorkOverlayHeaderPacket& packet)
{
    LowWorkOverlayHeaderTextPacket outPacket{};
    SeedLowWorkOverlayHeaderTextPacket(packet, outPacket);
    return outPacket;
}

inline void SeedHighWorkOverlayTextPacket(const HighWorkOverlayPacket& packet,
                                          HighWorkOverlayTextPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.initUnknown = packet.initUnknown;
    outPacket.gameplayAuto = packet.gameplayAuto;
    outPacket.ui = packet.ui;
    outPacket.track = packet.track;
    outPacket.car = packet.car;
    outPacket.finishSync = packet.finishSync;
    outPacket.freeBytes = packet.freeBytes;
}

inline HighWorkOverlayTextPacket BuildHighWorkOverlayTextPacket(const HighWorkOverlayPacket& packet)
{
    HighWorkOverlayTextPacket outPacket{};
    SeedHighWorkOverlayTextPacket(packet, outPacket);
    return outPacket;
}

inline void SeedLowWorkOverlayBreakdownTextPacket(const LowWorkOverlayBreakdownPacket& packet,
                                                  LowWorkOverlayBreakdownTextPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.renderers = packet.renderers;
    outPacket.slotState = packet.slotState;
    outPacket.workingSet = packet.workingSet;
    outPacket.familyCache = packet.familyCache;
    outPacket.transient = packet.transient;
    outPacket.metadata = packet.metadata;
}

inline LowWorkOverlayBreakdownTextPacket BuildLowWorkOverlayBreakdownTextPacket(
    const LowWorkOverlayBreakdownPacket& packet)
{
    LowWorkOverlayBreakdownTextPacket outPacket{};
    SeedLowWorkOverlayBreakdownTextPacket(packet, outPacket);
    return outPacket;
}

inline void SeedLowWorkOverlayTicksTextPacket(const LowWorkOverlayTicksPacket& packet,
                                              LowWorkOverlayTicksTextPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.trackStreamTicks = packet.trackStreamTicks;
    outPacket.trackMaintenanceTicks = packet.trackMaintenanceTicks;
    outPacket.trackDrawTicks = packet.trackDrawTicks;
    outPacket.trackFrameTicks = packet.trackFrameTicks;
    outPacket.trackWindowTicks = packet.trackWindowTicks;
    outPacket.trackPrefetchTicks = packet.trackPrefetchTicks;
    outPacket.trackLodTicks = packet.trackLodTicks;
    outPacket.trackWorkingSetTicks = packet.trackWorkingSetTicks;
    outPacket.prefetchBuildAttempts = packet.prefetchBuildAttempts;
    outPacket.prefetchBuildBudget = packet.prefetchBuildBudget;
    outPacket.prefetchBuildDrops = packet.prefetchBuildDrops;
}

inline LowWorkOverlayTicksTextPacket BuildLowWorkOverlayTicksTextPacket(
    const LowWorkOverlayTicksPacket& packet)
{
    LowWorkOverlayTicksTextPacket outPacket{};
    SeedLowWorkOverlayTicksTextPacket(packet, outPacket);
    return outPacket;
}

inline void SeedLowWorkTagGroupTextPacket(const LowWorkTagGroupPacket& packet,
                                          LowWorkTagGroupTextPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.initUnknown = packet.initUnknown;
    outPacket.gameplayAuto = packet.gameplayAuto;
    outPacket.ui = packet.ui;
    outPacket.track = packet.track;
    outPacket.car = packet.car;
    outPacket.finishSync = packet.finishSync;
}

inline LowWorkTagGroupTextPacket BuildLowWorkTagGroupTextPacket(const LowWorkTagGroupPacket& packet)
{
    LowWorkTagGroupTextPacket outPacket{};
    SeedLowWorkTagGroupTextPacket(packet, outPacket);
    return outPacket;
}

inline void SeedLowWorkAllocatorTextPacket(const LowWorkAllocatorPacket& packet,
                                           LowWorkAllocatorTextPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.payloadBytes = packet.payloadBytes;
    outPacket.overheadBytes = packet.overheadBytes;
    outPacket.freeBlocks = packet.freeBlocks;
    outPacket.knownTaggedBytes = packet.knownTaggedBytes;
    outPacket.invalidTaggedBytes = packet.invalidTaggedBytes;
    outPacket.invalidTaggedBlocks = packet.invalidTaggedBlocks;
}

inline LowWorkAllocatorTextPacket BuildLowWorkAllocatorTextPacket(const LowWorkAllocatorPacket& packet)
{
    LowWorkAllocatorTextPacket outPacket{};
    SeedLowWorkAllocatorTextPacket(packet, outPacket);
    return outPacket;
}

inline void SeedLowWorkOverlayTextBundle(const LowWorkOverlayHeaderPacket& header,
                                         const HighWorkOverlayPacket& highWork,
                                         const LowWorkOverlayBreakdownPacket& breakdown,
                                         const LowWorkOverlayTicksPacket& ticks,
                                         const LowWorkTagGroupPacket& tagGroups,
                                         const LowWorkAllocatorPacket& allocator,
                                         LowWorkOverlayTextBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.header = BuildLowWorkOverlayHeaderTextPacket(header);
    outBundle.highWork = BuildHighWorkOverlayTextPacket(highWork);
    outBundle.breakdown = BuildLowWorkOverlayBreakdownTextPacket(breakdown);
    outBundle.ticks = BuildLowWorkOverlayTicksTextPacket(ticks);
    outBundle.tagGroups = BuildLowWorkTagGroupTextPacket(tagGroups);
    outBundle.allocator = BuildLowWorkAllocatorTextPacket(allocator);
}

inline LowWorkOverlayTextBundle BuildLowWorkOverlayTextBundle(
    const LowWorkOverlayHeaderPacket& header,
    const HighWorkOverlayPacket& highWork,
    const LowWorkOverlayBreakdownPacket& breakdown,
    const LowWorkOverlayTicksPacket& ticks,
    const LowWorkTagGroupPacket& tagGroups,
    const LowWorkAllocatorPacket& allocator)
{
    LowWorkOverlayTextBundle bundle{};
    SeedLowWorkOverlayTextBundle(header, highWork, breakdown, ticks, tagGroups, allocator, bundle);
    return bundle;
}

} // namespace GameLoopMemoryPresentationDomain
