#pragma once

#include "memory_budget_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct RenderBudgetPresentationViewPacket
{
    bool valid = false;
    MemoryBudgetDomain::AllocationPool trackPreferredPool = MemoryBudgetDomain::AllocationPool::LowWork;
    MemoryBudgetDomain::AllocationPool carPreferredPool = MemoryBudgetDomain::AllocationPool::LowWork;
    bool trackShouldReducePressure = false;
    bool carShouldReducePressure = false;
    bool trackShouldAvoidOptionalAllocations = false;
    bool carShouldAvoidOptionalAllocations = false;
    bool anyRenderShouldReducePressure = false;
    bool anyRenderShouldAvoidOptionalAllocations = false;
};

} // namespace GameLoopObservabilityDomain
