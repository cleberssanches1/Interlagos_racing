#pragma once

#include "memory_budget_contracts.hpp"
#include "memory_budget_category_assembler.hpp"
#include "memory_budget_policy_assembler.hpp"
#include "memory_budget_telemetry_assembler.hpp"

// Operacoes passivas externas para futura reintroducao de `MemoryBudgetSystem`.
// Nao devem ser integradas ao runtime critico nesta fase.

namespace MemoryBudgetDomain
{

inline Thresholds BuildThresholds(uint32_t highWorkSoftFloor,
                                  uint32_t highWorkHardFloor,
                                  uint32_t highWorkCatastrophicFloor,
                                  uint32_t lowWorkSoftFloor,
                                  uint32_t lowWorkHardFloor,
                                  uint32_t pcmPreferredHighWorkBlock = 192u * 1024u)
{
    Thresholds thresholds{};
    thresholds.highWorkSoftFloor = highWorkSoftFloor;
    thresholds.highWorkHardFloor = highWorkHardFloor;
    thresholds.highWorkCatastrophicFloor = highWorkCatastrophicFloor;
    thresholds.lowWorkSoftFloor = lowWorkSoftFloor;
    thresholds.lowWorkHardFloor = lowWorkHardFloor;
    thresholds.pcmPreferredHighWorkBlock = pcmPreferredHighWorkBlock;
    return thresholds;
}

inline MemorySnapshotPacket CaptureMemorySnapshotPacket()
{
    MemorySnapshotPacket packet{};
    const auto snapshot = Game::MemoryBudgetSystem::CaptureSnapshot();
    SeedMemorySnapshotPacket(snapshot,
                             Game::MemoryBudgetSystem::HighWorkLargestFreeBlock(),
                             Game::MemoryBudgetSystem::HighWorkFreeSpace(),
                             Game::MemoryBudgetSystem::CartFreeSpace(),
                             Game::MemoryBudgetSystem::HasCartRam(),
                             packet);
    return packet;
}

inline MemoryPressurePacket BuildMemoryPressurePacket(const MemorySnapshotPacket& snapshot,
                                                      const Thresholds& thresholds)
{
    MemoryPressurePacket packet{};
    SeedMemoryPressurePacket(snapshot, thresholds, packet);
    return packet;
}

inline MemoryBudgetPolicyPacket BuildMemoryPolicyPacket(const MemorySnapshotPacket& snapshot,
                                                        const MemoryPressurePacket& pressure,
                                                        const Thresholds& thresholds)
{
    MemoryBudgetPolicyPacket packet{};
    SeedMemoryBudgetPolicyPacket(snapshot, pressure, thresholds, packet);
    return packet;
}

inline MemoryTelemetryPacket BuildMemoryTelemetryPacket(const MemorySnapshotPacket& snapshot,
                                                        const MemoryPressurePacket& pressure)
{
    MemoryTelemetryPacket packet{};
    SeedMemoryTelemetryPacket(snapshot, pressure, packet);
    return packet;
}

inline CategoryBudgetPolicyPacket BuildCategoryBudgetPolicyPacket(
    const MemorySnapshotPacket& snapshot,
    const MemoryPressurePacket& pressure,
    const MemoryBudgetPolicyPacket& policy)
{
    CategoryBudgetPolicyPacket packet{};
    SeedCategoryBudgetPolicyPacket(snapshot, pressure, policy, packet);
    return packet;
}

inline CategoryBudgetPolicy BuildCategoryBudgetPolicy(const ConsumerCategory category,
                                                      const MemorySnapshotPacket& snapshot,
                                                      const MemoryPressurePacket& pressure,
                                                      const MemoryBudgetPolicyPacket& policy)
{
    CategoryBudgetPolicy categoryPolicy{};
    SeedCategoryBudgetPolicy(category, snapshot, pressure, policy, categoryPolicy);
    return categoryPolicy;
}

} // namespace MemoryBudgetDomain
