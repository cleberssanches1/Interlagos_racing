#pragma once

#include <srl.hpp>

#include "game_loop_debug_state.hpp"
#include "game_loop_memory_presentation_contracts.hpp"

namespace GameLoopMemoryPresentationDomain
{

struct LowWorkOverlayRuntimePacket
{
    bool valid = false;
    uint32_t freeBytes = 0u;
    uint32_t highFreeBytes = 0u;
    uint8_t slides = 0u;
    int16_t slideId = -1;
    TrackSystem::LowWorkCategoryBreakdown breakdown{};
    LowWorkOverlayTicksPacket ticks{};
};

struct LowWorkOverlayRuntimeStateUpdate
{
    LowWorkOverlayRuntimePacket runtimeOverlay{};
    int32_t freeDelta = 0;
};

struct LowWorkOverlayMemoryDebugPacket;

inline void ApplyLowWorkOverlayBreakdownState(
    GameLoopRuntime::LowWorkOverlayState& overlay,
    const TrackSystem::LowWorkCategoryBreakdown& breakdown);

inline void ApplyLowWorkOverlayMemoryDebugState(
    GameLoopRuntime::LowWorkOverlayState& overlay,
    const LowWorkOverlayMemoryDebugPacket& packet);

inline LowWorkOverlayRuntimePacket CaptureLowWorkOverlayRuntimePacket(
    const TrackSystem* trackSystem,
    bool trackSystemReady,
    const MemoryBudgetDomain::MemorySnapshotPacket& memorySnapshot)
{
    LowWorkOverlayRuntimePacket packet{};
    packet.valid = true;
    packet.highFreeBytes = memorySnapshot.snapshot.highWorkFree;

    if (trackSystem != nullptr && trackSystemReady)
    {
        packet.freeBytes = trackSystem->LowWorkEndFreeBytesThisFrame();
        packet.slides = trackSystem->SlidesThisFrame();
        packet.slideId = static_cast<int16_t>(trackSystem->SlideSegmentIdThisFrame());
        packet.breakdown = trackSystem->LowWorkBreakdownThisFrame();
        packet.ticks.valid = true;
        packet.ticks.trackStreamTicks = trackSystem->StreamTicksThisFrame();
        packet.ticks.trackMaintenanceTicks = trackSystem->MaintenanceTicksThisFrame();
        packet.ticks.trackDrawTicks = trackSystem->DrawTicksThisFrame();
        packet.ticks.trackFrameTicks = trackSystem->FrameTicksThisFrame();
        packet.ticks.trackWindowTicks = trackSystem->WindowTicksThisFrame();
        packet.ticks.trackPrefetchTicks = trackSystem->PrefetchTicksThisFrame();
        packet.ticks.trackLodTicks = trackSystem->LodTicksThisFrame();
        packet.ticks.trackWorkingSetTicks = trackSystem->WorkingSetTicksThisFrame();
        packet.ticks.prefetchBuildAttempts = trackSystem->PrefetchBuildAttemptsThisFrame();
        packet.ticks.prefetchBuildBudget = trackSystem->PrefetchBuildBudgetThisFrame();
        packet.ticks.prefetchBuildDrops = trackSystem->PrefetchBuildBudgetDropsThisFrame();
        return packet;
    }

    packet.freeBytes = memorySnapshot.snapshot.lowWorkFree;
    return packet;
}

inline LowWorkOverlayRuntimeStateUpdate CaptureAndApplyLowWorkOverlayRuntimeState(
    GameLoopRuntime::LowWorkOverlayState& overlay,
    const TrackSystem* trackSystem,
    bool trackSystemReady)
{
    const auto memorySnapshot = MemoryBudgetDomain::CaptureMemorySnapshotPacket();
    LowWorkOverlayRuntimeStateUpdate update{};
    update.runtimeOverlay = CaptureLowWorkOverlayRuntimePacket(
        trackSystem,
        trackSystemReady,
        memorySnapshot);
    update.freeDelta = overlay.FreeValid()
        ? (static_cast<int32_t>(update.runtimeOverlay.freeBytes) -
           static_cast<int32_t>(overlay.lastFreeBytes))
        : 0;
    overlay.lastFreeBytes = update.runtimeOverlay.freeBytes;
    overlay.SetFreeValid(true);
    ApplyLowWorkOverlayBreakdownState(overlay, update.runtimeOverlay.breakdown);
    return update;
}

struct LowWorkTrackTagBytesPacket
{
    uint32_t trackCoreBytes = 0u;
    uint32_t trackPrepareBytes = 0u;
    uint32_t trackLodBytes = 0u;
    uint32_t trackTextureBytes = 0u;
    uint32_t trackBackendBytes = 0u;
};

struct LowWorkOverlayMemoryDebugPacket
{
    bool valid = false;
    LowWorkTrackTagBytesPacket trackBytes{};
    LowWorkTagGroupPacket tagGroups{};
    LowWorkAllocatorPacket allocator{};
};

inline LowWorkTrackTagBytesPacket CaptureLowWorkTrackTagBytesPacket()
{
    LowWorkTrackTagBytesPacket packet{};
    packet.trackCoreBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackCore));
    packet.trackPrepareBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackPrepare));
    packet.trackLodBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackLod));
    packet.trackTextureBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackTexture));
    packet.trackBackendBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackBackend));
    return packet;
}

inline void PresentLowWorkTrackTagBytesPacket(const LowWorkTrackTagBytesPacket& packet)
{
    SRL::Debug::Print(2, 18, "LWT1 tc:%u tp:%u tl:%u   ",
                      static_cast<unsigned>(packet.trackCoreBytes),
                      static_cast<unsigned>(packet.trackPrepareBytes),
                      static_cast<unsigned>(packet.trackLodBytes));
    SRL::Debug::Print(2, 19, "LWT2 tx:%u tb:%u         ",
                      static_cast<unsigned>(packet.trackTextureBytes),
                      static_cast<unsigned>(packet.trackBackendBytes));
}

inline LowWorkTagGroupPacket CaptureLowWorkTagGroupPacket(const LowWorkTrackTagBytesPacket& trackBytes)
{
    LowWorkTagGroupPacket packet{};
    packet.valid = true;
    packet.initUnknown =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Init)) +
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Unknown));
    packet.gameplayAuto =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Gameplay)) +
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::AutoLap));
    packet.ui =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Background)) +
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Hud));
    packet.car =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Car));
    packet.track =
        trackBytes.trackCoreBytes +
        trackBytes.trackPrepareBytes +
        trackBytes.trackLodBytes +
        trackBytes.trackTextureBytes +
        trackBytes.trackBackendBytes;
    packet.finishSync =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Finish)) +
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Sync));
    return packet;
}

inline GameLoopRuntime::LowWorkTagGroupOverlay BuildLowWorkTagGroupOverlay(
    const LowWorkTagGroupPacket& packet)
{
    GameLoopRuntime::LowWorkTagGroupOverlay overlay{};
    overlay.initUnknown = packet.initUnknown;
    overlay.gameplayAuto = packet.gameplayAuto;
    overlay.ui = packet.ui;
    overlay.car = packet.car;
    overlay.track = packet.track;
    overlay.finishSync = packet.finishSync;
    return overlay;
}

inline void ApplyLowWorkOverlayBreakdownState(
    GameLoopRuntime::LowWorkOverlayState& overlay,
    const TrackSystem::LowWorkCategoryBreakdown& breakdown)
{
    overlay.lastBreakdown = breakdown;
    overlay.SetBreakdownValid(true);
}

inline void ApplyLowWorkOverlayMemoryDebugState(
    GameLoopRuntime::LowWorkOverlayState& overlay,
    const LowWorkOverlayMemoryDebugPacket& packet)
{
    overlay.lastTagGroup = BuildLowWorkTagGroupOverlay(packet.tagGroups);
    overlay.SetTagGroupValid(true);
    overlay.lastPayloadBytes = packet.allocator.payloadBytes;
    overlay.lastOverheadBytes = packet.allocator.overheadBytes;
    overlay.lastFreeBlocks = packet.allocator.freeBlocks;
    overlay.SetAllocatorValid(true);
}

inline LowWorkAllocatorPacket CaptureLowWorkAllocatorPacket(const LowWorkTagGroupPacket& tagGroups)
{
    const auto lwrReport = SRL::Memory::LowWorkRam::GetReport();
    const uint32_t usedBytes = static_cast<uint32_t>(
        (lwrReport.TotalSize >= lwrReport.FreeSize) ? (lwrReport.TotalSize - lwrReport.FreeSize) : 0u);
    const uint32_t payloadBytes = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedPayloadBytes());

    LowWorkAllocatorPacket packet{};
    packet.valid = true;
    packet.payloadBytes = payloadBytes;
    packet.overheadBytes = (usedBytes >= payloadBytes) ? (usedBytes - payloadBytes) : 0u;
    packet.freeBlocks = static_cast<uint32_t>(lwrReport.FreeBlocks);
    packet.knownTaggedBytes =
        tagGroups.initUnknown +
        tagGroups.gameplayAuto +
        tagGroups.ui +
        tagGroups.car +
        tagGroups.track +
        tagGroups.finishSync;
    packet.invalidTaggedBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesWithInvalidTag());
    packet.invalidTaggedBlocks =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBlockCountWithInvalidTag());
    return packet;
}

inline LowWorkOverlayMemoryDebugPacket CaptureLowWorkOverlayMemoryDebugPacket()
{
    LowWorkOverlayMemoryDebugPacket packet{};
    packet.valid = true;
    packet.trackBytes = CaptureLowWorkTrackTagBytesPacket();
    packet.tagGroups = CaptureLowWorkTagGroupPacket(packet.trackBytes);
    packet.allocator = CaptureLowWorkAllocatorPacket(packet.tagGroups);
    return packet;
}

inline LowWorkOverlayMemoryDebugPacket CaptureAndApplyLowWorkOverlayMemoryDebugState(
    GameLoopRuntime::LowWorkOverlayState& overlay)
{
    const auto packet = CaptureLowWorkOverlayMemoryDebugPacket();
    ApplyLowWorkOverlayMemoryDebugState(overlay, packet);
    return packet;
}

} // namespace GameLoopMemoryPresentationDomain
