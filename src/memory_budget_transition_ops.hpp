#pragma once

#include "memory_budget_contracts.hpp"
#include "memory_budget_category_assembler.hpp"
#include "memory_budget_policy_assembler.hpp"
#include "memory_budget_telemetry_assembler.hpp"

// Operacoes passivas externas para futura reintroducao de `MemoryBudgetSystem`.
// Nao devem ser integradas ao runtime critico nesta fase.

namespace MemoryBudgetDomain
{

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

} // namespace MemoryBudgetDomain
