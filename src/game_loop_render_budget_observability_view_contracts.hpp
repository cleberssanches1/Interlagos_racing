#pragma once

#include "memory_budget_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct RenderBudgetConsumerViewPacket
{
    bool valid = false;
    MemoryBudgetDomain::AllocationPool preferredPool = MemoryBudgetDomain::AllocationPool::LowWork;
    bool shouldReducePressure = false;
    bool shouldAvoidOptionalAllocations = false;
};

struct RenderBudgetObservabilityViewPacket
{
    bool valid = false;
    RenderBudgetConsumerViewPacket track{};
    RenderBudgetConsumerViewPacket car{};
};

} // namespace GameLoopObservabilityDomain
