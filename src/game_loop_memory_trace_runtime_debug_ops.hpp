#pragma once

#include <srl.hpp>

#include "game_loop_memory_debug_presenter_ops.hpp"
#include "game_loop_memory_presentation_state_assembler.hpp"
#include "game_loop_memory_trace_packet_assembler.hpp"
#include "game_loop_memory_trace_text_assembler.hpp"
#include "game_loop_memory_trace_text_view_assembler.hpp"
#include "memory_budget_transition_ops.hpp"
#include "track_system.hpp"

namespace GameLoopMemoryPresentationDomain
{

struct HighWorkLiveTagPacket
{
    uint32_t initBytes = 0u;
    uint32_t unknownBytes = 0u;
    uint32_t backgroundBytes = 0u;
    uint32_t hudBytes = 0u;
    uint32_t gameplayBytes = 0u;
    uint32_t autoLapBytes = 0u;
    uint32_t finishBytes = 0u;
    uint32_t syncBytes = 0u;
};

inline HighWorkLiveTagPacket CaptureHighWorkLiveTagPacket()
{
    HighWorkLiveTagPacket packet{};
    packet.initBytes =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Init));
    packet.unknownBytes =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Unknown));
    packet.backgroundBytes =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Background));
    packet.hudBytes =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Hud));
    packet.gameplayBytes =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Gameplay));
    packet.autoLapBytes =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::AutoLap));
    packet.finishBytes =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Finish));
    packet.syncBytes =
        static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Sync));
    return packet;
}

inline void PresentHighWorkLiveTagPacket(const HighWorkLiveTagPacket& packet)
{
    SRL::Debug::Print(2, 16, "H1 i:%u u:%u bg:%u hd:%u ",
                      static_cast<unsigned>(packet.initBytes),
                      static_cast<unsigned>(packet.unknownBytes),
                      static_cast<unsigned>(packet.backgroundBytes),
                      static_cast<unsigned>(packet.hudBytes));
    SRL::Debug::Print(2, 17, "H2 gp:%u au:%u fn:%u sy:%u ",
                      static_cast<unsigned>(packet.gameplayBytes),
                      static_cast<unsigned>(packet.autoLapBytes),
                      static_cast<unsigned>(packet.finishBytes),
                      static_cast<unsigned>(packet.syncBytes));
}

inline void PresentHighWorkTracePacket(const HighWorkTracePacket& packet)
{
    PresentHighWorkTraceTextViewPacket(
        BuildHighWorkTraceTextViewPacket(
            BuildHighWorkTraceTextPacket(packet)));
}

struct LowWorkValidationDebugPacket
{
    bool validationValid = false;
    uint32_t trackBackendBytes = 0u;
    uint32_t trackPrepareBytes = 0u;
    uint32_t trackLodBytes = 0u;
    uint32_t trackTextureBytes = 0u;
    uint32_t initBytes = 0u;
    uint32_t unknownBytes = 0u;
    uint32_t gameplayBytes = 0u;
    uint32_t autoLapBytes = 0u;
    uint32_t payloadBytes = 0u;
    uint32_t overheadBytes = 0u;
    uint32_t largestFreeBytes = 0u;
    uint32_t freeBlocks = 0u;
    uint32_t blockOffset = 0u;
    uint32_t nextOffset = 0u;
    uint32_t blockSize = 0u;
    uint32_t freeBytes = 0u;
};

inline LowWorkValidationDebugPacket CaptureLowWorkValidationDebugPacket()
{
    LowWorkValidationDebugPacket packet{};
    const auto validation = SRL::Memory::LowWorkRam::Validate();
    const auto lwrReport = SRL::Memory::LowWorkRam::GetReport();
    const uint32_t lwrUsedBytesDirect = static_cast<uint32_t>(
        (lwrReport.TotalSize >= lwrReport.FreeSize) ? (lwrReport.TotalSize - lwrReport.FreeSize) : 0u);
    const uint32_t payloadBytesDirect = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedPayloadBytes());

    packet.validationValid = validation.valid;
    packet.trackBackendBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackBackend));
    packet.trackPrepareBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackPrepare));
    packet.trackLodBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackLod));
    packet.trackTextureBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackTexture));
    packet.initBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Init));
    packet.unknownBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Unknown));
    packet.gameplayBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Gameplay));
    packet.autoLapBytes =
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::AutoLap));

    if (packet.validationValid)
    {
        packet.payloadBytes = payloadBytesDirect;
        packet.overheadBytes =
            (lwrUsedBytesDirect >= payloadBytesDirect) ? (lwrUsedBytesDirect - payloadBytesDirect) : 0u;
        packet.largestFreeBytes = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetLargestFreeBlockSize());
        packet.freeBlocks = static_cast<uint32_t>(lwrReport.FreeBlocks);
        return packet;
    }

    const auto memorySnapshot = MemoryBudgetDomain::CaptureMemorySnapshotPacket();
    packet.blockOffset = static_cast<uint32_t>(validation.blockOffset);
    packet.nextOffset = static_cast<uint32_t>(validation.nextOffset);
    packet.blockSize = static_cast<uint32_t>(validation.blockSize);
    packet.freeBytes = memorySnapshot.snapshot.lowWorkFree;
    return packet;
}

inline void PresentLowWorkValidationDebugPacket(const LowWorkValidationDebugPacket& packet)
{
    SRL::Debug::Print(2, 20, "L7 tb:%u tw:%u tl:%u tx:%u",
                      static_cast<unsigned>(packet.trackBackendBytes),
                      static_cast<unsigned>(packet.trackPrepareBytes),
                      static_cast<unsigned>(packet.trackLodBytes),
                      static_cast<unsigned>(packet.trackTextureBytes));
    SRL::Debug::Print(2, 21, "L8 i:%u u:%u gp:%u au:%u",
                      static_cast<unsigned>(packet.initBytes),
                      static_cast<unsigned>(packet.unknownBytes),
                      static_cast<unsigned>(packet.gameplayBytes),
                      static_cast<unsigned>(packet.autoLapBytes));
    if (packet.validationValid)
    {
        SRL::Debug::Print(2, 22, "L10 py:%u ov:%u lf:%u fb:%u",
                          static_cast<unsigned>(packet.payloadBytes),
                          static_cast<unsigned>(packet.overheadBytes),
                          static_cast<unsigned>(packet.largestFreeBytes),
                          static_cast<unsigned>(packet.freeBlocks));
    }
    else
    {
        SRL::Debug::Print(2, 22, "L9 bo:%u nx:%u bs:%u fr:%u ",
                          static_cast<unsigned>(packet.blockOffset),
                          static_cast<unsigned>(packet.nextOffset),
                          static_cast<unsigned>(packet.blockSize),
                          static_cast<unsigned>(packet.freeBytes));
    }
}

inline void PresentLowWorkTracePacket(const LowWorkTracePacket& packet,
                                      const LowWorkTraceDeltaInputs& inputs)
{
    PresentLowWorkTraceTextViewPacket(
        BuildLowWorkTraceTextViewPacket(
            BuildLowWorkTraceTextPacket(packet, inputs)));
}

} // namespace GameLoopMemoryPresentationDomain
