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
static constexpr uint8_t kLod0 = 3u;
static constexpr uint8_t kLod1 = 1u;
static constexpr uint8_t kLod2 = 2u;
static constexpr uint8_t kLod32 = kLod2;
static constexpr uint8_t kLod64 = kLod0;

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

struct WindowRingState
{
    int32_t startSegmentId = 1;
    size_t headIndex = 0u;
    int8_t direction = 1;
    int8_t physicalStep = 1;
};

enum class StagedSlideAction : uint8_t
{
    None = 0u,
    PreparePrefetch,
    PrepareSlide,
    CommitSlide
};

// A slide crosses three frame boundaries: renderer prefetch, immutable
// back-buffer preparation, then publication into the resident window.
constexpr StagedSlideAction ResolveStagedSlideAction(int32_t backlog,
                                                     bool prefetchTargetMatches,
                                                     bool prefetchMetadataReady,
                                                     bool prefetchRendererReady,
                                                     bool slideBackBufferReady) noexcept
{
    if (backlog <= 0) return StagedSlideAction::None;
    if (slideBackBufferReady) return StagedSlideAction::CommitSlide;
    if (prefetchTargetMatches && prefetchMetadataReady && prefetchRendererReady)
    {
        return StagedSlideAction::PrepareSlide;
    }
    return StagedSlideAction::PreparePrefetch;
}

// Conservative draw-only rejection.  Streaming/residency is intentionally
// independent: a segment is rejected only when its complete XZ AABB lies
// behind an expanded camera plane.  Using the AABB support point instead of
// the segment center preserves long scenery and folded/hairpin segments.
constexpr bool IsAabbEntirelyBehindCameraXZ(int32_t cameraXRaw,
                                            int32_t cameraZRaw,
                                            int32_t forwardXRaw,
                                            int32_t forwardZRaw,
                                            int32_t minXRaw,
                                            int32_t maxXRaw,
                                            int32_t minZRaw,
                                            int32_t maxZRaw,
                                            int32_t marginRaw) noexcept
{
    if ((forwardXRaw == 0 && forwardZRaw == 0) ||
        minXRaw > maxXRaw || minZRaw > maxZRaw)
    {
        return false;
    }

    constexpr int32_t kFxpOne = 1 << 16;
    auto toWorldUnits = [](int32_t raw) -> int32_t { return raw / kFxpOne; };
    const int32_t minX = toWorldUnits(minXRaw);
    const int32_t maxX = toWorldUnits(maxXRaw);
    const int32_t minZ = toWorldUnits(minZRaw);
    const int32_t maxZ = toWorldUnits(maxZRaw);
    const int32_t centerX = minX + ((maxX - minX) / 2);
    const int32_t centerZ = minZ + ((maxZ - minZ) / 2);
    const int32_t extentX = (maxX - minX) / 2;
    const int32_t extentZ = (maxZ - minZ) / 2;
    int32_t forwardX = toWorldUnits(forwardXRaw);
    int32_t forwardZ = toWorldUnits(forwardZRaw);
    if (forwardX == 0 && forwardXRaw != 0) forwardX = (forwardXRaw < 0) ? -1 : 1;
    if (forwardZ == 0 && forwardZRaw != 0) forwardZ = (forwardZRaw < 0) ? -1 : 1;

    // Keep all products in cheap 32-bit arithmetic on SH-2. Direction scale
    // does not affect the half-plane decision.
    int32_t absForwardX = (forwardX < 0) ? -forwardX : forwardX;
    int32_t absForwardZ = (forwardZ < 0) ? -forwardZ : forwardZ;
    const int32_t maxForward = (absForwardX > absForwardZ) ? absForwardX : absForwardZ;
    if (maxForward > 1024)
    {
        const int32_t divisor = (maxForward + 1023) / 1024;
        forwardX /= divisor;
        forwardZ /= divisor;
        absForwardX = (forwardX < 0) ? -forwardX : forwardX;
        absForwardZ = (forwardZ < 0) ? -forwardZ : forwardZ;
    }

    // Maximum dot product of any AABB point against the camera forward vector.
    const int32_t maxProjected =
        (centerX - toWorldUnits(cameraXRaw)) * forwardX +
        (centerZ - toWorldUnits(cameraZRaw)) * forwardZ +
        extentX * absForwardX +
        extentZ * absForwardZ;
    const int32_t safeMargin = (marginRaw > 0) ? toWorldUnits(marginRaw) : 0;
    const int32_t expandedPlane = safeMargin * (absForwardX + absForwardZ);
    return maxProjected < -expandedPlane;
}

// Decide whether a surface query must expand from its local neighborhood to
// the already-resident streaming window. A strict wheel query cannot consume
// an outside-face planar fallback, so that fallback must not suppress recovery.
constexpr bool ShouldRunResidentSurfaceRecovery(bool foundInside,
                                                 bool foundFallback,
                                                 bool allowFallback) noexcept
{
    if (foundInside) return false;
    return !foundFallback || !allowFallback;
}

// Camera heading alone is ambiguous while crossing a long hairpin inside one
// logical segment. Use it to anticipate a deliberate U-turn only while the car
// is stationary/slow; real opposite segment progression remains authoritative
// at every speed.
constexpr bool ShouldAllowHeadingOnlyDirectionFlip(bool anchorUnchanged,
                                                    bool speedProxyValid,
                                                    uint16_t planarUnitsPerFrame,
                                                    uint16_t maxHeadingOnlyUnitsPerFrame) noexcept
{
    if (!anchorUnchanged) return false;
    if (!speedProxyValid) return true;
    return planarUnitsPerFrame <= maxHeadingOnlyUnitsPerFrame;
}

// A single segment reported behind the current anchor is not sufficient at
// speed: wheel contacts can briefly select the preceding face where a hairpin
// folds back beside itself.  Two segments of opposite progression are
// unambiguous; at low speed one segment is enough so a deliberate U-turn does
// not wait unnecessarily for another boundary crossing.
constexpr bool ShouldConfirmOppositeDirectionProgress(int32_t oppositeProgress,
                                                       bool speedProxyValid,
                                                       uint16_t planarUnitsPerFrame,
                                                       uint16_t maxSingleStepUnitsPerFrame) noexcept
{
    if (oppositeProgress <= 0 || oppositeProgress > 2) return false;
    if (oppositeProgress >= 2) return true;
    if (!speedProxyValid) return false;
    return planarUnitsPerFrame <= maxSingleStepUnitsPerFrame;
}

// Reverse gear walks segment ids against the belt while the chase camera still
// looks along the nose / increasing-route tangent (video 134618). Opposite
// progress must NOT flip construction when TDIR still agrees with the current
// belt — only a real heading turnaround (routeDesired != current) may.
constexpr bool ShouldApplyOppositeProgressFlip(bool routeTangentValid,
                                                int8_t routeDesiredDirection,
                                                int8_t currentDirection,
                                                int32_t oppositeProgress,
                                                bool speedProxyValid,
                                                uint16_t planarUnitsPerFrame,
                                                uint16_t maxSingleStepUnitsPerFrame) noexcept
{
    currentDirection = (currentDirection < 0) ? -1 : 1;
    routeDesiredDirection = (routeDesiredDirection < 0) ? -1 : 1;
    if (routeTangentValid && routeDesiredDirection == currentDirection)
    {
        return false;
    }
    return ShouldConfirmOppositeDirectionProgress(
        oppositeProgress,
        speedProxyValid,
        planarUnitsPerFrame,
        maxSingleStepUnitsPerFrame);
}

constexpr int64_t SeamAbs64(int64_t value) noexcept
{
    return value < 0 ? -value : value;
}

// Shrink a 2D vector so later |n·t| products fit in int64.
constexpr void SeamScale15(int64_t& x, int64_t& z) noexcept
{
    int64_t magnitude = SeamAbs64(x);
    const int64_t az = SeamAbs64(z);
    if (az > magnitude) magnitude = az;
    if (magnitude <= 32767) return;
    x = (x * 32767) / magnitude;
    z = (z * 32767) / magnitude;
}

// Ignore a wall hit that blocks travel across a segment join.
// Ground faces never reach this test. A lateral stem (normal across the lane)
// stays active even when an endpoint touches the join. A stem whose normal
// faces along the lane is dropped only inside the end band (15% of the
// along-track AABB), matching WALL_SEAM_END_FRAC in the TCOL bake.
// travelX/Z is the neighbor-centroid delta in the same space as the contact
// and the AABB. A zero travel vector falls back to the shorter AABB axis.
constexpr bool ShouldRejectSeamTravelWall(int32_t wallNx,
                                           int32_t wallNz,
                                           int32_t contactX,
                                           int32_t contactZ,
                                           int32_t minX,
                                           int32_t maxX,
                                           int32_t minZ,
                                           int32_t maxZ,
                                           int32_t travelX,
                                           int32_t travelZ) noexcept
{
    int64_t tx = travelX;
    int64_t tz = travelZ;
    if (tx == 0 && tz == 0)
    {
        const int64_t xSpan = static_cast<int64_t>(maxX) - minX;
        const int64_t zSpan = static_cast<int64_t>(maxZ) - minZ;
        if (SeamAbs64(zSpan) <= SeamAbs64(xSpan)) tz = 1;
        else tx = 1;
    }
    int64_t nx = wallNx;
    int64_t nz = wallNz;
    SeamScale15(nx, nz);
    SeamScale15(tx, tz);
    const int64_t dot = (nx * tx) + (nz * tz);
    const int64_t n2 = (nx * nx) + (nz * nz);
    const int64_t t2 = (tx * tx) + (tz * tz);
    if (n2 <= 0 || t2 <= 0) return false;
    // |n·t| / (|n||t|) >= 0.70  <=>  dot^2 / 49 >= n2 * t2 / 100.
    const int64_t left = (dot * dot) / 49;
    const int64_t right = (n2 / 10) * (t2 / 10);
    if (left < right) return false;

    const bool travelIsZ = SeamAbs64(tz) >= SeamAbs64(tx);
    const int64_t span = travelIsZ
        ? (static_cast<int64_t>(maxZ) - minZ)
        : (static_cast<int64_t>(maxX) - minX);
    const int64_t useSpan = span > 1 ? span : 1;
    const int64_t band = (useSpan * 15) / 100;
    const int64_t coord = travelIsZ ? contactZ : contactX;
    const int64_t lo = travelIsZ ? minZ : minX;
    const int64_t hi = travelIsZ ? maxZ : maxX;
    const int64_t distLo = SeamAbs64(coord - lo);
    const int64_t distHi = SeamAbs64(coord - hi);
    const int64_t distEnd = distLo < distHi ? distLo : distHi;
    return distEnd <= band;
}

// The track direction map describes increasing logical segment ids.  Comparing
// the camera with that local tangent distinguishes a genuine U-turn from a
// hairpin: both the car and the tangent rotate together through a hairpin,
// while only the car reverses during a turnaround at the same location.
constexpr int8_t ResolveRouteRelativeDirection(int8_t currentDirection,
                                                bool routeTangentValid,
                                                int32_t cameraDotIncreasingRouteRaw,
                                                int32_t reverseEnterDotRaw) noexcept
{
    currentDirection = (currentDirection < 0) ? -1 : 1;
    if (!routeTangentValid || reverseEnterDotRaw <= 0) return currentDirection;
    const bool facesIncreasingRoute = cameraDotIncreasingRouteRaw >= reverseEnterDotRaw;
    const bool facesDecreasingRoute = cameraDotIncreasingRouteRaw <= -reverseEnterDotRaw;
    if (currentDirection > 0 && facesDecreasingRoute) return -1;
    if (currentDirection < 0 && facesIncreasingRoute) return 1;
    return currentDirection;
}

constexpr int32_t WrapSegmentIdToRange(int32_t segmentId, uint16_t totalSegmentCount) noexcept
{
    if (totalSegmentCount == 0u) return -1;
    const int32_t total = static_cast<int32_t>(totalSegmentCount);
    int32_t normalized = (segmentId - 1) % total;
    if (normalized < 0) normalized += total;
    return normalized + 1;
}

// Scenery-only logical ids sit in the lap order with no driveable ground.
// Interlagos 103–106 is that hole between asphalt 102 and 107. A raw ±1
// walk never reaches the far side. Bridge at most this many ids.
constexpr int32_t kDriveableSegmentBridge = 8;

// Next id in `step` (+1 or -1) whose predicate is true, skipping holes.
// `hasGround` receives a wrapped segment id and returns true when that
// segment has driveable collision ground.
template <typename HasGroundFn>
int32_t NextDriveableSegmentId(int32_t startId,
                               int32_t step,
                               int32_t total,
                               HasGroundFn hasGround)
{
    if (startId <= 0 || total <= 0 || step == 0) return -1;
    const int32_t direction = (step < 0) ? -1 : 1;
    for (int32_t hop = 1; hop <= kDriveableSegmentBridge; ++hop)
    {
        const int32_t candidate = WrapSegmentIdToRange(
            startId + direction * hop,
            static_cast<uint16_t>(total));
        if (candidate <= 0 || candidate == startId) return -1;
        if (hasGround(candidate)) return candidate;
    }
    return -1;
}

constexpr size_t LogicalToPhysicalWindowIndex(size_t headIndex,
                                               int8_t physicalStep,
                                               size_t logicalIndex,
                                               size_t windowCount) noexcept
{
    if (windowCount == 0u) return 0u;
    const size_t head = headIndex % windowCount;
    const size_t rank = logicalIndex % windowCount;
    return (physicalStep < 0)
        ? ((head + windowCount - rank) % windowCount)
        : ((head + rank) % windowCount);
}

constexpr int32_t ResolveWindowIncomingSegmentId(int32_t startSegmentId,
                                                  uint16_t totalSegmentCount,
                                                  size_t windowCount,
                                                  int8_t direction) noexcept
{
    const int32_t dir = (direction < 0) ? -1 : 1;
    return WrapSegmentIdToRange(
        startSegmentId + (dir * static_cast<int32_t>(windowCount)),
        totalSegmentCount);
}

constexpr WindowRingState ReorientWindowRing(WindowRingState state,
                                              uint16_t totalSegmentCount,
                                              size_t windowCount,
                                              int8_t newDirection) noexcept
{
    newDirection = (newDirection < 0) ? -1 : 1;
    state.direction = (state.direction < 0) ? -1 : 1;
    state.physicalStep = (state.physicalStep < 0) ? -1 : 1;
    if (windowCount == 0u || totalSegmentCount == 0u || state.direction == newDirection)
    {
        state.direction = newDirection;
        return state;
    }

    const size_t tailRank = windowCount - 1u;
    state.headIndex = LogicalToPhysicalWindowIndex(
        state.headIndex, state.physicalStep, tailRank, windowCount);
    state.startSegmentId = WrapSegmentIdToRange(
        state.startSegmentId +
            (static_cast<int32_t>(state.direction) * static_cast<int32_t>(tailRank)),
        totalSegmentCount);
    state.direction = newDirection;
    state.physicalStep = static_cast<int8_t>(-state.physicalStep);
    return state;
}

constexpr WindowRingState AdvanceWindowRing(WindowRingState state,
                                            uint16_t totalSegmentCount,
                                            size_t windowCount) noexcept
{
    if (windowCount == 0u || totalSegmentCount == 0u) return state;
    state.direction = (state.direction < 0) ? -1 : 1;
    state.physicalStep = (state.physicalStep < 0) ? -1 : 1;
    state.startSegmentId = WrapSegmentIdToRange(
        state.startSegmentId + static_cast<int32_t>(state.direction),
        totalSegmentCount);
    state.headIndex = LogicalToPhysicalWindowIndex(
        state.headIndex, state.physicalStep, 1u, windowCount);
    return state;
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
    const size_t lod0End = static_cast<size_t>(config.designLod0Count);
    const size_t lod1End = static_cast<size_t>(config.lod64Count);

    // Runtime slot indexes are deliberately independent from texture dimensions.
    if (rank < lod0End) return kLod0;
    if (rank < lod1End) return kLod1;
    return kLod2;
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
        static_cast<size_t>(config.designLod0Count),
        static_cast<size_t>(config.lod64Count),
    }};

    plan.reserve(boundaryRanks.size());
    for (size_t i = 0; i < boundaryRanks.size(); ++i)
    {
        const size_t logicalRank = boundaryRanks[i];
        if (logicalRank == 0u || logicalRank >= windowCount) continue;
        BoundaryPrewarmTarget target{};
        target.logicalRank = logicalRank;
        // Prewarm the bank required by the segment entering the new band.
        target.targetLodIndex = ResolveLodIndexByRank(logicalRank, config);
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
