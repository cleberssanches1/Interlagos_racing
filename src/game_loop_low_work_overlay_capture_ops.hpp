#pragma once

#include <srl.hpp>

#include "game_loop_debug_state.hpp"
#include "game_loop_memory_presentation_contracts.hpp"

namespace GameLoopMemoryPresentationDomain
{

struct LowWorkTrackTagBytesPacket
{
    uint32_t trackCoreBytes = 0u;
    uint32_t trackPrepareBytes = 0u;
    uint32_t trackLodBytes = 0u;
    uint32_t trackTextureBytes = 0u;
    uint32_t trackBackendBytes = 0u;
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

} // namespace GameLoopMemoryPresentationDomain
