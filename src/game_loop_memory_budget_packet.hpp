#pragma once

#include "memory_budget_contracts.hpp"

namespace GameLoopRuntime
{

struct MemoryBudgetFramePacket
{
    MemoryBudgetDomain::MemorySnapshotPacket snapshot{};
    MemoryBudgetDomain::MemoryPressurePacket pressure{};
    MemoryBudgetDomain::MemoryBudgetPolicyPacket policy{};
    MemoryBudgetDomain::CategoryBudgetPolicyPacket categories{};
    MemoryBudgetDomain::MemoryTelemetryPacket telemetry{};

    bool Valid() const
    {
        return snapshot.valid && pressure.valid && policy.valid && telemetry.valid;
    }
};

} // namespace GameLoopRuntime
