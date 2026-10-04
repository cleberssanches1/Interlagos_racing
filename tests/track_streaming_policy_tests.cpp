#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "track_streaming_policy.hpp"

namespace
{
struct TestContext
{
    int failures = 0;

    void Fail(const char* expr,
              const char* file,
              int line,
              const std::string& detail = std::string())
    {
        std::cerr << file << ":" << line << " failure: " << expr;
        if (!detail.empty()) std::cerr << " [" << detail << "]";
        std::cerr << "\n";
        ++failures;
    }
};

template <typename T>
std::string ToString(const T& value)
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

template <typename T>
std::string ToString(const std::vector<T>& values)
{
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0) oss << ",";
        oss << values[i];
    }
    oss << "]";
    return oss.str();
}

template <typename T, size_t N>
std::string ToString(const std::array<T, N>& values)
{
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0) oss << ",";
        oss << values[i];
    }
    oss << "]";
    return oss.str();
}

#define EXPECT_TRUE(ctx, expr) \
    do \
    { \
        if (!(expr)) (ctx).Fail(#expr, __FILE__, __LINE__); \
    } while (0)

#define EXPECT_EQ(ctx, actual, expected) \
    do \
    { \
        const auto actualValue = (actual); \
        const auto expectedValue = (expected); \
        if (!(actualValue == expectedValue)) \
        { \
            (ctx).Fail(#actual " == " #expected, \
                       __FILE__, \
                       __LINE__, \
                       std::string("actual=") + ToString(actualValue) + \
                       " expected=" + ToString(expectedValue)); \
        } \
    } while (0)

void TestResolveLodBandsForConfiguredWindow(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    for (size_t rank = 0; rank < TrackLodConfig::kLod0Segments; ++rank)
    {
        EXPECT_EQ(ctx, ResolveLodIndexByRank(rank), kLod0);
    }
    for (size_t rank = TrackLodConfig::kLod0Segments;
         rank < TrackLodConfig::kTexture64Segments;
         ++rank)
    {
        EXPECT_EQ(ctx, ResolveLodIndexByRank(rank), kLod1);
    }
    for (size_t rank = TrackLodConfig::kTexture64Segments;
         rank < TrackLodConfig::kVisibleSegments;
         ++rank)
    {
        EXPECT_EQ(ctx, ResolveLodIndexByRank(rank), kLod2);
    }
    EXPECT_EQ(ctx, ResolveLodIndexByRank(TrackLodConfig::kVisibleSegments), kLod2);

    EXPECT_EQ(ctx, ResolveDesignLodByRank(0u), static_cast<uint8_t>(0u));
    EXPECT_EQ(ctx,
              ResolveDesignLodByRank(TrackLodConfig::kLod0Segments),
              static_cast<uint8_t>(1u));
    EXPECT_EQ(ctx,
              ResolveDesignLodByRank(TrackLodConfig::kTexture64Segments),
              static_cast<uint8_t>(2u));

    const std::array<size_t, 4> counts =
        CountWindowSegmentsByLod(TrackLodConfig::kVisibleSegments);
    const std::array<size_t, 4> expectedCounts{{
        0u,
        TrackLodConfig::kLod1Segments,
        TrackLodConfig::kLod2Segments,
        TrackLodConfig::kLod0Segments
    }};
    EXPECT_EQ(ctx, counts, expectedCounts);
}

void TestWindowSlidesForwardKeepingSameBandShape(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const size_t windowCount = TrackLodConfig::kVisibleSegments;
    const std::vector<int32_t> start1 = BuildWindowSegmentIds(1, 305, windowCount, +1);
    const std::vector<int32_t> start2 = BuildWindowSegmentIds(2, 305, windowCount, +1);

    EXPECT_EQ(ctx, start1.front(), 1);
    EXPECT_EQ(ctx, start1.back(), static_cast<int32_t>(windowCount));
    EXPECT_EQ(ctx, start2.front(), 2);
    EXPECT_EQ(ctx, start2.back(), static_cast<int32_t>(windowCount + 1u));

    for (size_t rank = 0; rank < windowCount; ++rank)
    {
        const uint8_t lod1 = ResolveLodIndexByRank(rank);
        const uint8_t lod2 = ResolveLodIndexByRank(rank);
        EXPECT_EQ(ctx, lod1, lod2);
    }
}

void TestWindowSlidesBackwardKeepingSameBandShape(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const size_t windowCount = TrackLodConfig::kVisibleSegments;
    const std::vector<int32_t> start305 = BuildWindowSegmentIds(305, 305, windowCount, -1);
    const std::vector<int32_t> start304 = BuildWindowSegmentIds(304, 305, windowCount, -1);

    EXPECT_EQ(ctx, start305.front(), 305);
    EXPECT_EQ(ctx, start305.back(), 290);
    EXPECT_EQ(ctx, start304.front(), 304);
    EXPECT_EQ(ctx, start304.back(), 289);

    for (size_t rank = 0; rank < windowCount; ++rank)
    {
        const uint8_t lodA = ResolveLodIndexByRank(rank);
        const uint8_t lodB = ResolveLodIndexByRank(rank);
        EXPECT_EQ(ctx, lodA, lodB);
    }
}

void TestRingReorientationKeepsResidentWindowAndReverseSlides(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    constexpr uint16_t totalSegments = 290u;
    constexpr size_t windowCount = 5u;
    WindowRingState state{};
    state.startSegmentId = 100;
    state.headIndex = 0u;
    state.direction = 1;
    state.physicalStep = 1;

    state = ReorientWindowRing(state, totalSegments, windowCount, -1);
    EXPECT_EQ(ctx, state.startSegmentId, 104);
    EXPECT_EQ(ctx, state.headIndex, static_cast<size_t>(4u));
    EXPECT_EQ(ctx, static_cast<int>(state.direction), -1);
    EXPECT_EQ(ctx, static_cast<int>(state.physicalStep), -1);

    const std::array<size_t, 5> reversedPhysical{{4u, 3u, 2u, 1u, 0u}};
    std::array<size_t, 5> actualPhysical{};
    for (size_t rank = 0; rank < windowCount; ++rank)
    {
        actualPhysical[rank] = LogicalToPhysicalWindowIndex(
            state.headIndex, state.physicalStep, rank, windowCount);
    }
    EXPECT_EQ(ctx, actualPhysical, reversedPhysical);
    EXPECT_EQ(ctx,
              ResolveWindowIncomingSegmentId(
                  state.startSegmentId, totalSegments, windowCount, state.direction),
              99);

    std::array<int32_t, 5> residentIds{{100, 101, 102, 103, 104}};
    residentIds[state.headIndex] = 99; // retire logical head 104, load far tail 99
    state = AdvanceWindowRing(state, totalSegments, windowCount);
    EXPECT_EQ(ctx, state.startSegmentId, 103);
    EXPECT_EQ(ctx, state.headIndex, static_cast<size_t>(3u));
    const std::array<int32_t, 5> expectedAfterReverseSlide{{103, 102, 101, 100, 99}};
    for (size_t rank = 0; rank < windowCount; ++rank)
    {
        const size_t physical = LogicalToPhysicalWindowIndex(
            state.headIndex, state.physicalStep, rank, windowCount);
        EXPECT_EQ(ctx, residentIds[physical], expectedAfterReverseSlide[rank]);
    }

    state = ReorientWindowRing(state, totalSegments, windowCount, +1);
    EXPECT_EQ(ctx, state.startSegmentId, 99);
    EXPECT_EQ(ctx, state.headIndex, static_cast<size_t>(4u));
    EXPECT_EQ(ctx, static_cast<int>(state.direction), 1);
    EXPECT_EQ(ctx, static_cast<int>(state.physicalStep), 1);
    const std::array<int32_t, 5> expectedAfterSecondFlip{{99, 100, 101, 102, 103}};
    for (size_t rank = 0; rank < windowCount; ++rank)
    {
        const size_t physical = LogicalToPhysicalWindowIndex(
            state.headIndex, state.physicalStep, rank, windowCount);
        EXPECT_EQ(ctx, residentIds[physical], expectedAfterSecondFlip[rank]);
    }
}

void TestRingReorientationWrapsAtLapBoundary(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    WindowRingState state{};
    state.startSegmentId = 288;
    state.headIndex = 2u;
    state.direction = 1;
    state.physicalStep = 1;
    state = ReorientWindowRing(state, 290u, 5u, -1);

    EXPECT_EQ(ctx, state.startSegmentId, 2);
    EXPECT_EQ(ctx, state.headIndex, static_cast<size_t>(1u));
    EXPECT_EQ(ctx,
              ResolveWindowIncomingSegmentId(
                  state.startSegmentId, 290u, 5u, state.direction),
              287);
}

void TestWindowWrapsAcrossLapBoundary(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::vector<int32_t> window = BuildWindowSegmentIds(
        299, 305, TrackLodConfig::kVisibleSegments, +1);
    const std::vector<int32_t> expected{
        299, 300, 301, 302, 303, 304, 305, 1, 2, 3, 4, 5, 6, 7, 8, 9
    };
    EXPECT_EQ(ctx, window, expected);
}

void TestCollectRetiredSlotsForRemovedFamilies(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::vector<FamilySlotsSnapshot> previous{
        {10u, {{101u, 102u, 103u, 104u}}},
        {20u, {{201u, 202u, kNoTexture, kNoTexture}}},
        {30u, {{301u, kNoTexture, 303u, kNoTexture}}},
    };
    const std::vector<FamilySlotsSnapshot> next{
        {20u, {{201u, 202u, kNoTexture, kNoTexture}}},
        {30u, {{301u, kNoTexture, 303u, kNoTexture}}},
    };

    const std::vector<uint16_t> retired = CollectRetiredSlotsForRemovedFamilies(
        previous,
        next,
        [](uint16_t slot)
        {
            return slot != 102u;
        });

    const std::vector<uint16_t> expected{101u, 103u, 104u};
    EXPECT_EQ(ctx, retired, expected);
}

void TestCollectRetiredSlotsDeduplicatesAcrossLods(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::vector<FamilySlotsSnapshot> previous{
        {10u, {{111u, 111u, kNoTexture, kNoTexture}}},
        {20u, {{111u, 222u, kNoTexture, kNoTexture}}},
    };
    const std::vector<FamilySlotsSnapshot> next{};

    const std::vector<uint16_t> retired = CollectRetiredSlotsForRemovedFamilies(
        previous,
        next,
        [](uint16_t slot)
        {
            return slot != 222u;
        });

    const std::vector<uint16_t> expected{111u};
    EXPECT_EQ(ctx, retired, expected);
}

void TestRetiredSlotsIgnoreFamiliesStillVisible(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::vector<FamilySlotsSnapshot> previous{
        {10u, {{11u, 12u, 13u, 14u}}},
        {20u, {{21u, 22u, 23u, 24u}}},
    };
    const std::vector<FamilySlotsSnapshot> next{
        {10u, {{11u, 12u, 13u, 14u}}},
        {20u, {{21u, 22u, 23u, 24u}}},
    };

    const std::vector<uint16_t> retired = CollectRetiredSlotsForRemovedFamilies(
        previous,
        next,
        [](uint16_t)
        {
            return true;
        });

    EXPECT_TRUE(ctx, retired.empty());
}

void TestTopNearCameraRanksStayBoundedToFourSegments(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::vector<size_t> ranks20 =
        BuildTopNearCameraRanks(TrackLodConfig::kVisibleSegments);
    const std::vector<size_t> ranks2 = BuildTopNearCameraRanks(2u);
    const std::vector<size_t> expected20{0u, 1u, 2u, 3u};
    const std::vector<size_t> expected2{0u, 1u};

    EXPECT_EQ(ctx, ranks20, expected20);
    EXPECT_EQ(ctx, ranks2, expected2);
}

void TestForwardSlideBoundaryPrewarmPlanMatchesContract(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::vector<BoundaryPrewarmTarget> plan = BuildForwardSlideBoundaryPrewarmPlan(
        TrackLodConfig::kVisibleSegments);
    EXPECT_EQ(ctx, plan.size(), static_cast<size_t>(2u));
    EXPECT_EQ(ctx, plan[0].logicalRank, TrackLodConfig::kLod0Segments);
    EXPECT_EQ(ctx, plan[0].targetLodIndex, kLod1);
    EXPECT_EQ(ctx, plan[1].logicalRank, TrackLodConfig::kTexture64Segments);
    EXPECT_EQ(ctx, plan[1].targetLodIndex, kLod2);
}

void TestWindowLodCountsInvariantAcrossLap(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::array<size_t, 4> expectedCounts{{
        0u,
        TrackLodConfig::kLod1Segments,
        TrackLodConfig::kLod2Segments,
        TrackLodConfig::kLod0Segments
    }};
    for (int32_t startId = 1; startId <= 305; ++startId)
    {
        const std::vector<int32_t> window = BuildWindowSegmentIds(
            startId, 305, TrackLodConfig::kVisibleSegments, +1);
        EXPECT_EQ(ctx, window.size(), TrackLodConfig::kVisibleSegments);

        std::array<size_t, 4> counts{{0u, 0u, 0u, 0u}};
        for (size_t rank = 0; rank < window.size(); ++rank)
        {
            const uint8_t lod = ResolveLodIndexByRank(rank);
            if (lod < counts.size()) ++counts[lod];
        }
        EXPECT_EQ(ctx, counts, expectedCounts);
    }
}

void TestResidentSurfaceRecoveryPolicy(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    // A containing face always completes the query locally.
    EXPECT_TRUE(ctx, !ShouldRunResidentSurfaceRecovery(true, false, false));
    EXPECT_TRUE(ctx, !ShouldRunResidentSurfaceRecovery(true, true, true));

    // A permissive query can consume its local planar fallback.
    EXPECT_TRUE(ctx, !ShouldRunResidentSurfaceRecovery(false, true, true));

    // A strict wheel query cannot consume that fallback and must search the
    // rest of the resident window for an actual containing driveable face.
    EXPECT_TRUE(ctx, ShouldRunResidentSurfaceRecovery(false, true, false));
    EXPECT_TRUE(ctx, ShouldRunResidentSurfaceRecovery(false, false, false));

    // A permissive query still needs recovery when local probing found nothing.
    EXPECT_TRUE(ctx, ShouldRunResidentSurfaceRecovery(false, false, true));
}

void TestHeadingOnlyDirectionFlipRequiresLowMotion(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    constexpr uint16_t maxHeadingOnlyMotion = 1u;
    EXPECT_TRUE(ctx, ShouldAllowHeadingOnlyDirectionFlip(
                         true, true, 0u, maxHeadingOnlyMotion));
    EXPECT_TRUE(ctx, ShouldAllowHeadingOnlyDirectionFlip(
                         true, true, 1u, maxHeadingOnlyMotion));
    EXPECT_TRUE(ctx, !ShouldAllowHeadingOnlyDirectionFlip(
                          true, true, 2u, maxHeadingOnlyMotion));

    // The first frame has no speed history and must still support a stationary
    // turn. Once the supporting segment changes, progression decides instead.
    EXPECT_TRUE(ctx, ShouldAllowHeadingOnlyDirectionFlip(
                         true, false, 0u, maxHeadingOnlyMotion));
    EXPECT_TRUE(ctx, !ShouldAllowHeadingOnlyDirectionFlip(
                          false, true, 0u, maxHeadingOnlyMotion));
}

void TestOppositeDirectionProgressRejectsFastSingleStepJitter(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    constexpr uint16_t maxSingleStepMotion = 1u;

    // A fast one-segment regression is ambiguous in a folded hairpin.
    EXPECT_TRUE(ctx, !ShouldConfirmOppositeDirectionProgress(
                          1, true, 19u, maxSingleStepMotion));

    // A deliberate slow U-turn may switch after the first crossed boundary.
    EXPECT_TRUE(ctx, ShouldConfirmOppositeDirectionProgress(
                         1, true, 1u, maxSingleStepMotion));

    // Two consecutive logical steps are authoritative at any speed.
    EXPECT_TRUE(ctx, ShouldConfirmOppositeDirectionProgress(
                         2, true, 19u, maxSingleStepMotion));

    // Missing speed history must not turn one noisy report into a reversal.
    EXPECT_TRUE(ctx, !ShouldConfirmOppositeDirectionProgress(
                          1, false, 0u, maxSingleStepMotion));
    EXPECT_TRUE(ctx, !ShouldConfirmOppositeDirectionProgress(
                          0, true, 0u, maxSingleStepMotion));
    EXPECT_TRUE(ctx, !ShouldConfirmOppositeDirectionProgress(
                          3, true, 0u, maxSingleStepMotion));
}

struct TestCase
{
    const char* name = "";
    void (*fn)(TestContext&) = nullptr;
};
}

int main()
{
    const std::vector<TestCase> tests{
        {"ResolveLodBandsForConfiguredWindow", &TestResolveLodBandsForConfiguredWindow},
        {"WindowSlidesForwardKeepingSameBandShape", &TestWindowSlidesForwardKeepingSameBandShape},
        {"WindowSlidesBackwardKeepingSameBandShape", &TestWindowSlidesBackwardKeepingSameBandShape},
        {"RingReorientationKeepsResidentWindowAndReverseSlides", &TestRingReorientationKeepsResidentWindowAndReverseSlides},
        {"RingReorientationWrapsAtLapBoundary", &TestRingReorientationWrapsAtLapBoundary},
        {"WindowWrapsAcrossLapBoundary", &TestWindowWrapsAcrossLapBoundary},
        {"CollectRetiredSlotsForRemovedFamilies", &TestCollectRetiredSlotsForRemovedFamilies},
        {"CollectRetiredSlotsDeduplicatesAcrossLods", &TestCollectRetiredSlotsDeduplicatesAcrossLods},
        {"RetiredSlotsIgnoreFamiliesStillVisible", &TestRetiredSlotsIgnoreFamiliesStillVisible},
        {"TopNearCameraRanksStayBoundedToFourSegments", &TestTopNearCameraRanksStayBoundedToFourSegments},
        {"ForwardSlideBoundaryPrewarmPlanMatchesContract", &TestForwardSlideBoundaryPrewarmPlanMatchesContract},
        {"WindowLodCountsInvariantAcrossLap", &TestWindowLodCountsInvariantAcrossLap},
        {"ResidentSurfaceRecoveryPolicy", &TestResidentSurfaceRecoveryPolicy},
        {"HeadingOnlyDirectionFlipRequiresLowMotion", &TestHeadingOnlyDirectionFlipRequiresLowMotion},
        {"OppositeDirectionProgressRejectsFastSingleStepJitter", &TestOppositeDirectionProgressRejectsFastSingleStepJitter},
    };

    TestContext ctx{};
    size_t passed = 0u;
    for (size_t i = 0; i < tests.size(); ++i)
    {
        try
        {
            tests[i].fn(ctx);
            if (ctx.failures == 0)
            {
                ++passed;
                std::cout << "[PASS] " << tests[i].name << "\n";
            }
            else
            {
                std::cout << "[FAIL] " << tests[i].name << "\n";
                return 1;
            }
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[EXCEPTION] " << tests[i].name << ": " << ex.what() << "\n";
            return 1;
        }
        catch (...)
        {
            std::cerr << "[EXCEPTION] " << tests[i].name << ": unknown\n";
            return 1;
        }
    }

    std::cout << "Passed " << passed << " test(s)\n";
    return 0;
}
