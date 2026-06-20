#pragma once

#include "memory_budget_contracts.hpp"

// Assembler passivo da politica por categoria do budget de memoria.
// Nao participa do runtime atual; apenas prepara a futura extracao.

namespace MemoryBudgetDomain
{

inline AllocationPool ResolveCategoryPreferredPool(const ConsumerCategory category,
                                                   const MemorySnapshotPacket& snapshot,
                                                   const MemoryBudgetPolicyPacket& policy)
{
    switch (category)
    {
    case ConsumerCategory::AudioPcm:
        if (policy.preferHighWorkForPcm) return AllocationPool::HighWork;
        if (policy.preferCartForPcm) return AllocationPool::Cart;
        return AllocationPool::LowWork;
    case ConsumerCategory::CdStaging:
        return snapshot.cartAvailable ? AllocationPool::Cart : AllocationPool::HighWork;
    case ConsumerCategory::TrackRender:
    case ConsumerCategory::CarRender:
        return AllocationPool::HighWork;
    case ConsumerCategory::Hud:
    case ConsumerCategory::DebugTransient:
    default:
        return AllocationPool::LowWork;
    }
}

inline void SeedCategoryBudgetPolicy(const ConsumerCategory category,
                                     const MemorySnapshotPacket& snapshot,
                                     const MemoryPressurePacket& pressure,
                                     const MemoryBudgetPolicyPacket& policy,
                                     CategoryBudgetPolicy& outPolicy)
{
    outPolicy.category = category;
    outPolicy.preferredPool = ResolveCategoryPreferredPool(category, snapshot, policy);
    outPolicy.shouldReducePressure = false;
    outPolicy.shouldAvoidOptionalAllocations = policy.shouldAvoidOptionalAllocations;

    switch (category)
    {
    case ConsumerCategory::TrackRender:
    case ConsumerCategory::CarRender:
    case ConsumerCategory::AudioPcm:
    case ConsumerCategory::CdStaging:
        outPolicy.shouldReducePressure =
            policy.shouldReduceStreamingPressure || pressure.highWorkHasHardPressure;
        break;
    case ConsumerCategory::Hud:
        outPolicy.shouldReducePressure = pressure.lowWorkHasHardPressure;
        break;
    case ConsumerCategory::DebugTransient:
    default:
        outPolicy.shouldReducePressure =
            pressure.highWorkHasSoftPressure || pressure.lowWorkHasSoftPressure;
        break;
    }
}

inline void SeedCategoryBudgetPolicyPacket(const MemorySnapshotPacket& snapshot,
                                           const MemoryPressurePacket& pressure,
                                           const MemoryBudgetPolicyPacket& policy,
                                           CategoryBudgetPolicyPacket& outPacket)
{
    outPacket.valid = snapshot.valid && pressure.valid && policy.valid;
    outPacket.count = 0u;

    constexpr ConsumerCategory kCategories[] = {
        ConsumerCategory::TrackRender,
        ConsumerCategory::CarRender,
        ConsumerCategory::AudioPcm,
        ConsumerCategory::Hud,
        ConsumerCategory::CdStaging,
        ConsumerCategory::DebugTransient
    };

    for (size_t i = 0; i < (sizeof(kCategories) / sizeof(kCategories[0])); ++i)
    {
        SeedCategoryBudgetPolicy(kCategories[i],
                                 snapshot,
                                 pressure,
                                 policy,
                                 outPacket.entries[i]);
        ++outPacket.count;
    }
}

} // namespace MemoryBudgetDomain
