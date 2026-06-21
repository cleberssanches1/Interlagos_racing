#pragma once

#include "memory_budget_transition_ops.hpp"
#include "game_loop_memory_budget_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedMemoryBudgetFramePacket(
    const MemoryBudgetDomain::MemorySnapshotPacket& snapshot,
    const MemoryBudgetDomain::MemoryPressurePacket& pressure,
    const MemoryBudgetDomain::MemoryBudgetPolicyPacket& policy,
    const MemoryBudgetDomain::CategoryBudgetPolicyPacket& categories,
    const MemoryBudgetDomain::MemoryTelemetryPacket& telemetry,
    MemoryBudgetFramePacket& outPacket)
{
    outPacket.snapshot = snapshot;
    outPacket.pressure = pressure;
    outPacket.policy = policy;
    outPacket.categories = categories;
    outPacket.telemetry = telemetry;
}

inline MemoryBudgetFramePacket BuildMemoryBudgetFramePacket(
    const MemoryBudgetDomain::MemorySnapshotPacket& snapshot,
    const MemoryBudgetDomain::MemoryPressurePacket& pressure,
    const MemoryBudgetDomain::MemoryBudgetPolicyPacket& policy,
    const MemoryBudgetDomain::CategoryBudgetPolicyPacket& categories,
    const MemoryBudgetDomain::MemoryTelemetryPacket& telemetry)
{
    MemoryBudgetFramePacket packet{};
    SeedMemoryBudgetFramePacket(snapshot, pressure, policy, categories, telemetry, packet);
    return packet;
}

} // namespace GameLoopRuntime
