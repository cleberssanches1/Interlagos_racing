#pragma once

#include "memory_budget_contracts.hpp"

// Assemblers passivos do dominio de budget de memoria.
// Nao participam do runtime atual; apenas preparam a futura extracao.

namespace MemoryBudgetDomain
{

inline void SeedMemorySnapshotPacket(const Game::MemoryBudgetSystem::Snapshot& snapshot,
                                     uint32_t highWorkLargestFreeBlock,
                                     int32_t highWorkFreeSpace,
                                     int32_t cartFreeSpace,
                                     bool cartAvailable,
                                     MemorySnapshotPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.snapshot = snapshot;
    outPacket.highWorkLargestFreeBlock = highWorkLargestFreeBlock;
    outPacket.highWorkFreeSpace = highWorkFreeSpace;
    outPacket.cartFreeSpace = cartFreeSpace;
    outPacket.cartAvailable = cartAvailable;
}

inline PressureLevel ClassifyPressure(const uint32_t freeBytes,
                                      const uint32_t softFloor,
                                      const uint32_t hardFloor,
                                      const uint32_t catastrophicFloor)
{
    if (catastrophicFloor > 0u && freeBytes <= catastrophicFloor) return PressureLevel::Catastrophic;
    if (hardFloor > 0u && freeBytes <= hardFloor) return PressureLevel::Critical;
    if (hardFloor > 0u && freeBytes <= (hardFloor + (8u * 1024u))) return PressureLevel::Pressure;
    if (softFloor > 0u && freeBytes <= softFloor) return PressureLevel::Soft;
    return PressureLevel::Normal;
}

inline void SeedMemoryPressurePacket(const MemorySnapshotPacket& snapshot,
                                     const Thresholds& thresholds,
                                     MemoryPressurePacket& outPacket)
{
    outPacket.valid = snapshot.valid;
    outPacket.highWorkPressure = ClassifyPressure(snapshot.snapshot.highWorkFree,
                                                  thresholds.highWorkSoftFloor,
                                                  thresholds.highWorkHardFloor,
                                                  thresholds.highWorkCatastrophicFloor);
    outPacket.lowWorkPressure = ClassifyPressure(snapshot.snapshot.lowWorkFree,
                                                 thresholds.lowWorkSoftFloor,
                                                 thresholds.lowWorkHardFloor,
                                                 0u);
    outPacket.highWorkHasSoftPressure = (outPacket.highWorkPressure >= PressureLevel::Soft);
    outPacket.highWorkHasHardPressure = (outPacket.highWorkPressure >= PressureLevel::Pressure);
    outPacket.highWorkCatastrophic = (outPacket.highWorkPressure == PressureLevel::Catastrophic);
    outPacket.lowWorkHasSoftPressure = (outPacket.lowWorkPressure >= PressureLevel::Soft);
    outPacket.lowWorkHasHardPressure = (outPacket.lowWorkPressure >= PressureLevel::Pressure);
}

inline void SeedMemoryBudgetPolicyPacket(const MemorySnapshotPacket& snapshot,
                                         const MemoryPressurePacket& pressure,
                                         const Thresholds& thresholds,
                                         MemoryBudgetPolicyPacket& outPacket)
{
    outPacket.valid = snapshot.valid && pressure.valid;
    outPacket.cartAvailable = snapshot.cartAvailable;
    outPacket.preferHighWorkForPcm =
        snapshot.highWorkLargestFreeBlock >= thresholds.pcmPreferredHighWorkBlock;
    outPacket.preferCartForPcm =
        !outPacket.preferHighWorkForPcm && snapshot.cartAvailable;
    outPacket.shouldReduceStreamingPressure =
        pressure.highWorkHasHardPressure || pressure.lowWorkHasHardPressure;
    outPacket.shouldAvoidOptionalAllocations =
        pressure.highWorkCatastrophic || pressure.lowWorkHasHardPressure;
}

} // namespace MemoryBudgetDomain
