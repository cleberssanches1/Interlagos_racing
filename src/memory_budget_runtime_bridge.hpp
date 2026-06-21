#pragma once

#include <srl.hpp>

#include "memory_budget_transition_ops.hpp"

namespace Game
{

class MemoryBudgetRuntimeBridge final
{
public:
    using ConsumerCategory = MemoryBudgetDomain::ConsumerCategory;
    using AllocationPool = MemoryBudgetDomain::AllocationPool;
    using CategoryBudgetPolicy = MemoryBudgetDomain::CategoryBudgetPolicy;

    static void ConfigurePcmStreamingBudgetFromPolicy()
    {
        const auto categoryPolicy = QueryCategoryPolicy(ConsumerCategory::AudioPcm);

        SRL::Sound::Pcm::SetMemAllocationBehaviour(
            SRL::Sound::Pcm::PcmMalloc::LwRam,
            ToPcmMalloc(categoryPolicy.preferredPool));
    }

    static bool ShouldPreferCartForCdStaging()
    {
        return PreferredPoolForCategory(ConsumerCategory::CdStaging) == AllocationPool::Cart;
    }

    static bool ShouldAvoidDebugTransientOptionalTelemetry()
    {
        return ShouldAvoidOptionalAllocationsForCategory(ConsumerCategory::DebugTransient);
    }

    static bool ShouldAvoidHudOptionalTelemetry()
    {
        return ShouldAvoidOptionalAllocationsForCategory(ConsumerCategory::Hud);
    }

    static CategoryBudgetPolicy QueryCategoryPolicy(const ConsumerCategory category)
    {
        return BuildCategoryPolicy(category);
    }

    static AllocationPool PreferredPoolForCategory(const ConsumerCategory category)
    {
        return QueryCategoryPolicy(category).preferredPool;
    }

    static bool ShouldAvoidOptionalAllocationsForCategory(const ConsumerCategory category)
    {
        return QueryCategoryPolicy(category).shouldAvoidOptionalAllocations;
    }

private:
    static MemoryBudgetDomain::Thresholds BuildDefaultThresholds()
    {
        return MemoryBudgetDomain::BuildThresholds(
            0u,
            0u,
            0u,
            0u,
            0u);
    }

    static CategoryBudgetPolicy BuildCategoryPolicy(const ConsumerCategory category)
    {
        const auto snapshot = MemoryBudgetDomain::CaptureMemorySnapshotPacket();
        const auto thresholds = BuildDefaultThresholds();
        const auto pressure = MemoryBudgetDomain::BuildMemoryPressurePacket(snapshot, thresholds);
        const auto policy = MemoryBudgetDomain::BuildMemoryPolicyPacket(snapshot, pressure, thresholds);
        return MemoryBudgetDomain::BuildCategoryBudgetPolicy(
            category,
            snapshot,
            pressure,
            policy);
    }

    static SRL::Sound::Pcm::PcmMalloc ToPcmMalloc(
        const AllocationPool pool)
    {
        switch (pool)
        {
        case AllocationPool::HighWork:
            return SRL::Sound::Pcm::PcmMalloc::HwRam;
        case AllocationPool::Cart:
            return SRL::Sound::Pcm::PcmMalloc::CartRam;
        case AllocationPool::LowWork:
        default:
            return SRL::Sound::Pcm::PcmMalloc::LwRam;
        }
    }
};

} // namespace Game
