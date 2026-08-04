#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "track_lod_config.hpp"

namespace TrackStreamingPolicy
{
static constexpr uint16_t kNoTexture = 0u;
static constexpr uint8_t kLod32 = 2u;
static constexpr uint8_t kLod64 = 3u;

struct LodBandConfig
{
    // 3 design LODs mapped to the configured visible window:
    // ranks [0, designLod0Count)           → 64 (design lod_0 presentation)
    // ranks [designLod0Count, lod64Count)  → 64 (design lod_1)
    // ranks [lod64Count, lod64+lod32)      → 32 (design lod_2)
    uint32_t designLod0Count = static_cast<uint32_t>(TrackLodConfig::kLod0Segments);
    uint32_t lod64Count = static_cast<uint32_t>(TrackLodConfig::kTexture64Segments);
    uint32_t lod32Count = static_cast<uint32_t>(TrackLodConfig::kTexture32Segments);
};

struct FamilySlotsSnapshot
{
    uint16_t familyId = 0u;
    std::array<uint16_t, 4> lodSlots{{kNoTexture, kNoTexture, kNoTexture, kNoTexture}};
};

struct BoundaryPrewarmTarget
{
    size_t logicalRank = 0u;
    uint8_t targetLodIndex = kLod32;
};

constexpr int32_t WrapSegmentIdToRange(int32_t segmentId, uint16_t totalSegmentCount) noexcept
{
    if (totalSegmentCount == 0u) return -1;
    const int32_t total = static_cast<int32_t>(totalSegmentCount);
    int32_t normalized = (segmentId - 1) % total;
    if (normalized < 0) normalized += total;
    return normalized + 1;
}

// Design band: 0 = lod_0, 1 = lod_1, 2 = lod_2 (for telemetry / future dual-GEO).
constexpr uint8_t ResolveDesignLodByRank(size_t rank, const LodBandConfig& config = {}) noexcept
{
    const size_t d0 = static_cast<size_t>(config.designLod0Count);
    const size_t d1End = static_cast<size_t>(config.lod64Count);
    if (rank < d0) return 0u;
    if (rank < d1End) return 1u;
    return 2u;
}

constexpr uint8_t ResolveLodIndexByRank(size_t rank, const LodBandConfig& config = {}) noexcept
{
    const size_t lod64End = static_cast<size_t>(config.lod64Count);
    const size_t lod32End = lod64End + static_cast<size_t>(config.lod32Count);

    // Texture size only: lod_0 and lod_1 share 64×64; lod_2 uses 32×32.
    if (rank < lod64End) return kLod64;
    if (rank < lod32End) return kLod32;
    return kLod32;
}

inline std::array<size_t, 4> CountWindowSegmentsByLod(size_t windowCount,
                                                      const LodBandConfig& config = {})
{
    std::array<size_t, 4> counts{{0u, 0u, 0u, 0u}};
    for (size_t rank = 0; rank < windowCount; ++rank)
    {
        const uint8_t lod = ResolveLodIndexByRank(rank, config);
        if (lod < counts.size()) ++counts[lod];
    }
    return counts;
}

inline std::vector<int32_t> BuildWindowSegmentIds(int32_t startId,
                                                  uint16_t totalSegmentCount,
                                                  size_t windowCount,
                                                  int8_t direction)
{
    std::vector<int32_t> ids{};
    if (windowCount == 0u || totalSegmentCount == 0u) return ids;

    direction = (direction < 0) ? -1 : 1;
    const int32_t wrappedStart = WrapSegmentIdToRange(startId, totalSegmentCount);
    if (wrappedStart <= 0) return ids;

    ids.reserve(windowCount);
    for (size_t logicalRank = 0; logicalRank < windowCount; ++logicalRank)
    {
        const int32_t segmentId = WrapSegmentIdToRange(
            wrappedStart + (direction > 0
                ? static_cast<int32_t>(logicalRank)
                : -static_cast<int32_t>(logicalRank)),
            totalSegmentCount);
        ids.push_back(segmentId);
    }
    return ids;
}

inline std::vector<size_t> BuildTopNearCameraRanks(size_t windowCount, size_t topCount = 4u)
{
    std::vector<size_t> ranks{};
    const size_t count = (windowCount < topCount) ? windowCount : topCount;
    ranks.reserve(count);
    for (size_t rank = 0; rank < count; ++rank)
    {
        ranks.push_back(rank);
    }
    return ranks;
}

inline std::vector<BoundaryPrewarmTarget> BuildForwardSlideBoundaryPrewarmPlan(
    size_t windowCount,
    const LodBandConfig& config = {})
{
    std::vector<BoundaryPrewarmTarget> plan{};
    const std::array<size_t, 2> boundaryRanks{{
        static_cast<size_t>(config.lod64Count),
        static_cast<size_t>(config.lod64Count + config.lod32Count),
    }};

    plan.reserve(boundaryRanks.size());
    for (size_t i = 0; i < boundaryRanks.size(); ++i)
    {
        const size_t logicalRank = boundaryRanks[i];
        if (logicalRank == 0u || logicalRank >= windowCount) continue;
        BoundaryPrewarmTarget target{};
        target.logicalRank = logicalRank;
        target.targetLodIndex = ResolveLodIndexByRank(logicalRank - 1u, config);
        plan.push_back(target);
    }
    return plan;
}

template <typename IsLiveFn>
inline std::vector<uint16_t> CollectRetiredSlotsForRemovedFamilies(
    const std::vector<FamilySlotsSnapshot>& previousFamilies,
    const std::vector<FamilySlotsSnapshot>& nextFamilies,
    IsLiveFn isLive)
{
    std::set<uint16_t> nextFamilyIds{};
    for (size_t i = 0; i < nextFamilies.size(); ++i)
    {
        if (nextFamilies[i].familyId == 0u) continue;
        nextFamilyIds.insert(nextFamilies[i].familyId);
    }

    std::set<uint16_t> retiredSlots{};
    for (size_t i = 0; i < previousFamilies.size(); ++i)
    {
        const FamilySlotsSnapshot& family = previousFamilies[i];
        if (family.familyId == 0u) continue;
        if (nextFamilyIds.find(family.familyId) != nextFamilyIds.end()) continue;

        for (size_t li = 0; li < family.lodSlots.size(); ++li)
        {
            const uint16_t slot = family.lodSlots[li];
            if (slot == kNoTexture) continue;
            if (!isLive(slot)) continue;
            retiredSlots.insert(slot);
        }
    }

    return std::vector<uint16_t>(retiredSlots.begin(), retiredSlots.end());
}
}
