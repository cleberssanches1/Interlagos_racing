#pragma once

#include "memory_budget_contracts.hpp"

// Assembler passivo de telemetria do budget de memoria.
// Mantem o recorte fora de `GameLoopSystem` e `TrackSystem` ate futura integracao.

namespace MemoryBudgetDomain
{

inline void SeedMemoryTelemetryPacket(const MemorySnapshotPacket& snapshot,
                                      const MemoryPressurePacket& pressure,
                                      MemoryTelemetryPacket& outPacket)
{
    outPacket.valid = snapshot.valid && pressure.valid;
    outPacket.highWorkFree = snapshot.snapshot.highWorkFree;
    outPacket.lowWorkFree = snapshot.snapshot.lowWorkFree;
    outPacket.cartFree = snapshot.snapshot.cartFree;
    outPacket.highWorkLargestFreeBlock = snapshot.highWorkLargestFreeBlock;
    outPacket.highWorkPressure = pressure.highWorkPressure;
    outPacket.lowWorkPressure = pressure.lowWorkPressure;
}

} // namespace MemoryBudgetDomain
