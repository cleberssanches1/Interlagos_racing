#pragma once

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

} // namespace GameLoopRuntime
