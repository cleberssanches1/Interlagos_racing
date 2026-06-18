#pragma once

#include <cstdint>

#include <srl.hpp>

#include "game_loop_debug_state.hpp"
#include "memory_budget_transition_ops.hpp"

namespace GameLoopRuntime
{

inline HwrStageTrace::Snapshot CaptureHighWorkRamSnapshot()
{
    HwrStageTrace::Snapshot snapshot{};
    const auto memorySnapshot = MemoryBudgetDomain::CaptureMemorySnapshotPacket();
    const auto report = SRL::Memory::HighWorkRam::GetReport();
    snapshot.freeBytes = memorySnapshot.snapshot.highWorkFree;
#if defined(SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY) && SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY
    const auto stats = SRL::Memory::HighWorkRam::GetOpStats();
    snapshot.usedBlocks = static_cast<uint32_t>(report.UsedBlocks);
    snapshot.freeBlocks = static_cast<uint32_t>(report.FreeBlocks);
    snapshot.liveBytes = static_cast<uint32_t>(stats.LiveBytes);
    snapshot.allocCalls = static_cast<uint32_t>(stats.AllocCalls);
    snapshot.freeCalls = static_cast<uint32_t>(stats.FreeCalls);
    snapshot.reallocCalls = static_cast<uint32_t>(stats.ReallocCalls);
    snapshot.failedAllocCalls = static_cast<uint32_t>(stats.FailedAllocCalls);
#else
    (void)report;
#endif
    return snapshot;
}

inline LwrStageTrace::Snapshot CaptureLowWorkRamSnapshot(bool detailed = false)
{
    const auto memorySnapshot = MemoryBudgetDomain::CaptureMemorySnapshotPacket();
    const auto report = SRL::Memory::LowWorkRam::GetReport();
    LwrStageTrace::Snapshot snapshot{};
    snapshot.freeBytes = memorySnapshot.snapshot.lowWorkFree;
#if !defined(SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY) || !SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY
    (void)detailed;
    (void)report;
    return snapshot;
#else
    if (!detailed)
    {
        return snapshot;
    }

    const uint32_t usedBytes = static_cast<uint32_t>(
        (memorySnapshot.snapshot.lowWorkTotal >= memorySnapshot.snapshot.lowWorkFree)
            ? (memorySnapshot.snapshot.lowWorkTotal - memorySnapshot.snapshot.lowWorkFree)
            : 0u);
    const uint32_t payloadBytes = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedPayloadBytes());
    snapshot.payloadBytes = payloadBytes;
    snapshot.overheadBytes = (usedBytes >= payloadBytes) ? (usedBytes - payloadBytes) : 0u;
    snapshot.freeBlocks = static_cast<uint32_t>(report.FreeBlocks);
    snapshot.largestFreeBytes = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetLargestFreeBlockSize());
    return snapshot;
#endif
}

template <bool TDetailedEnabled>
inline HwrStageTrace::Snapshot MaybeCaptureHighWorkRamSnapshot()
{
    if constexpr (!TDetailedEnabled)
    {
        return {};
    }
    return CaptureHighWorkRamSnapshot();
}

template <bool TDetailedEnabled>
inline LwrStageTrace::Snapshot MaybeCaptureLowWorkRamSnapshot(bool detailed = false)
{
    if constexpr (!TDetailedEnabled)
    {
        (void)detailed;
        return {};
    }
    return CaptureLowWorkRamSnapshot(detailed);
}

inline int32_t SnapshotLiveDelta(const HwrStageTrace::Snapshot& from,
                                 const HwrStageTrace::Snapshot& to)
{
#if !defined(SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY) || !SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY
    (void)from;
    (void)to;
    return 0;
#else
    return static_cast<int32_t>(to.liveBytes) - static_cast<int32_t>(from.liveBytes);
#endif
}

inline int32_t SnapshotFreeDelta(const LwrStageTrace::Snapshot& from,
                                 const LwrStageTrace::Snapshot& to)
{
    return static_cast<int32_t>(to.freeBytes) - static_cast<int32_t>(from.freeBytes);
}

inline int32_t SnapshotPayloadDelta(const LwrStageTrace::Snapshot& from,
                                    const LwrStageTrace::Snapshot& to)
{
#if !defined(SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY) || !SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY
    (void)from;
    (void)to;
    return 0;
#else
    return static_cast<int32_t>(to.payloadBytes) - static_cast<int32_t>(from.payloadBytes);
#endif
}

inline int32_t SnapshotOverheadDelta(const LwrStageTrace::Snapshot& from,
                                     const LwrStageTrace::Snapshot& to)
{
#if !defined(SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY) || !SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY
    (void)from;
    (void)to;
    return 0;
#else
    return static_cast<int32_t>(to.overheadBytes) - static_cast<int32_t>(from.overheadBytes);
#endif
}

} // namespace GameLoopRuntime
