#pragma once

#include "memory_budget_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct RenderBudgetOverlayTextViewPacket
{
    bool valid = false;
    bool shouldShowTrackBudget = false;
    bool shouldShowCarBudget = false;
    bool shouldShowAnyRenderBudget = false;
    bool shouldShowAnyReducePressure = false;
    bool shouldShowAnyOptionalAllocationGuard = false;
    MemoryBudgetDomain::AllocationPool trackPreferredPool = MemoryBudgetDomain::AllocationPool::LowWork;
    MemoryBudgetDomain::AllocationPool carPreferredPool = MemoryBudgetDomain::AllocationPool::LowWork;
};

} // namespace GameLoopObservabilityDomain
