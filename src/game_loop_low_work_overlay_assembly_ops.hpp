#pragma once

#include <srl.hpp>

#include "game_loop_memory_overlay_text_assembler.hpp"
#include "game_loop_memory_presentation_state_assembler.hpp"
#include "game_loop_memory_presentation_contracts.hpp"
#include "track_system.hpp"

namespace GameLoopMemoryPresentationDomain
{

inline LowWorkOverlayHeaderPacket BuildLowWorkOverlayHeaderPacket(
    int32_t freeDelta,
    uint32_t lowWorkFree,
    uint32_t highWorkFree,
    uint8_t slides,
    int16_t slideId)
{
    LowWorkOverlayHeaderPacket packet{};
    SeedLowWorkOverlayHeaderPacket(freeDelta, lowWorkFree, highWorkFree, slides, slideId, packet);
    return packet;
}

inline LowWorkOverlayTextBundle BuildLowWorkOverlayTextBundleFromHeader(
    const LowWorkOverlayHeaderPacket& header)
{
    return BuildLowWorkOverlayTextBundle(header,
                                         HighWorkOverlayPacket{},
                                         LowWorkOverlayBreakdownPacket{},
                                         LowWorkOverlayTicksPacket{},
                                         LowWorkTagGroupPacket{},
                                         LowWorkAllocatorPacket{});
}

inline HighWorkOverlayPacket CaptureHighWorkOverlayPacket(uint32_t highWorkFreeBytes)
{
    HighWorkOverlayPacket packet{};
    packet.valid = true;
    packet.initUnknown =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Init)) +
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Unknown));
    packet.gameplayAuto =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Gameplay)) +
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::AutoLap));
    packet.ui =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Background)) +
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Hud));
    packet.track =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackCore)) +
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackPrepare)) +
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackLod)) +
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackTexture)) +
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackBackend));
    packet.car =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Car));
    packet.finishSync =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Finish)) +
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Sync));
    packet.freeBytes = highWorkFreeBytes;
    return packet;
}

inline LowWorkOverlayBreakdownPacket BuildLowWorkOverlayBreakdownPacket(
    const TrackSystem::LowWorkCategoryBreakdown& breakdown)
{
    LowWorkOverlayBreakdownPacket packet{};
    packet.valid = true;
    packet.renderers = breakdown.renderers;
    packet.slotState = breakdown.slotState;
    packet.workingSet = breakdown.workingSet;
    packet.familyCache = breakdown.familyCache;
    packet.transient = breakdown.transient;
    packet.metadata = breakdown.metadata;
    return packet;
}

inline LowWorkOverlayTicksPacket BuildLowWorkOverlayTicksPacket(
    uint16_t trackStreamTicks,
    uint16_t trackMaintenanceTicks,
    uint16_t trackDrawTicks,
    uint16_t trackFrameTicks,
    uint16_t trackWindowTicks,
    uint16_t trackPrefetchTicks,
    uint16_t trackLodTicks,
    uint16_t trackWorkingSetTicks,
    uint8_t prefetchBuildAttempts,
    uint8_t prefetchBuildBudget,
    uint8_t prefetchBuildDrops)
{
    LowWorkOverlayTicksPacket packet{};
    SeedLowWorkOverlayTicksPacket(trackStreamTicks,
                                  trackMaintenanceTicks,
                                  trackDrawTicks,
                                  trackFrameTicks,
                                  trackWindowTicks,
                                  trackPrefetchTicks,
                                  trackLodTicks,
                                  trackWorkingSetTicks,
                                  prefetchBuildAttempts,
                                  prefetchBuildBudget,
                                  prefetchBuildDrops,
                                  packet);
    return packet;
}

} // namespace GameLoopMemoryPresentationDomain
