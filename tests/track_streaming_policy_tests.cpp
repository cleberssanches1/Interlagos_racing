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

void TestResolveLodBandsForTwentySegmentWindow(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    for (size_t rank = 0; rank < 4u; ++rank)
    {
        EXPECT_EQ(ctx, ResolveLodIndexByRank(rank), kLod64);
    }
    for (size_t rank = 4u; rank < 9u; ++rank)
    {
        EXPECT_EQ(ctx, ResolveLodIndexByRank(rank), kLod32);
    }
    for (size_t rank = 9u; rank < 14u; ++rank)
    {
        EXPECT_EQ(ctx, ResolveLodIndexByRank(rank), kLod16);
    }
    for (size_t rank = 14u; rank < 20u; ++rank)
    {
        EXPECT_EQ(ctx, ResolveLodIndexByRank(rank), kLod8);
    }
    EXPECT_EQ(ctx, ResolveLodIndexByRank(20u), kLod8);

    const std::array<size_t, 4> counts = CountWindowSegmentsByLod(20u);
    const std::array<size_t, 4> expectedCounts{{6u, 5u, 5u, 4u}};
    EXPECT_EQ(ctx, counts, expectedCounts);
}

void TestWindowSlidesForwardKeepingSameBandShape(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::vector<int32_t> start1 = BuildWindowSegmentIds(1, 305, 20u, +1);
    const std::vector<int32_t> start2 = BuildWindowSegmentIds(2, 305, 20u, +1);

    EXPECT_EQ(ctx, start1.front(), 1);
    EXPECT_EQ(ctx, start1.back(), 20);
    EXPECT_EQ(ctx, start2.front(), 2);
    EXPECT_EQ(ctx, start2.back(), 21);

    for (size_t rank = 0; rank < 20u; ++rank)
    {
        const uint8_t lod1 = ResolveLodIndexByRank(rank);
        const uint8_t lod2 = ResolveLodIndexByRank(rank);
        EXPECT_EQ(ctx, lod1, lod2);
    }
}

void TestWindowSlidesBackwardKeepingSameBandShape(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::vector<int32_t> start305 = BuildWindowSegmentIds(305, 305, 20u, -1);
    const std::vector<int32_t> start304 = BuildWindowSegmentIds(304, 305, 20u, -1);

    EXPECT_EQ(ctx, start305.front(), 305);
    EXPECT_EQ(ctx, start305.back(), 286);
    EXPECT_EQ(ctx, start304.front(), 304);
    EXPECT_EQ(ctx, start304.back(), 285);

    for (size_t rank = 0; rank < 20u; ++rank)
    {
        const uint8_t lodA = ResolveLodIndexByRank(rank);
        const uint8_t lodB = ResolveLodIndexByRank(rank);
        EXPECT_EQ(ctx, lodA, lodB);
    }
}

void TestWindowWrapsAcrossLapBoundary(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::vector<int32_t> window = BuildWindowSegmentIds(299, 305, 20u, +1);
    const std::vector<int32_t> expected{
        299, 300, 301, 302, 303, 304, 305, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
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

    const std::vector<size_t> ranks20 = BuildTopNearCameraRanks(20u);
    const std::vector<size_t> ranks2 = BuildTopNearCameraRanks(2u);
    const std::vector<size_t> expected20{0u, 1u, 2u, 3u};
    const std::vector<size_t> expected2{0u, 1u};

    EXPECT_EQ(ctx, ranks20, expected20);
    EXPECT_EQ(ctx, ranks2, expected2);
}

void TestForwardSlideBoundaryPrewarmPlanMatchesContract(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::vector<BoundaryPrewarmTarget> plan = BuildForwardSlideBoundaryPrewarmPlan(20u);
    EXPECT_EQ(ctx, plan.size(), static_cast<size_t>(3u));
    EXPECT_EQ(ctx, plan[0].logicalRank, static_cast<size_t>(4u));
    EXPECT_EQ(ctx, plan[0].targetLodIndex, kLod64);
    EXPECT_EQ(ctx, plan[1].logicalRank, static_cast<size_t>(9u));
    EXPECT_EQ(ctx, plan[1].targetLodIndex, kLod32);
    EXPECT_EQ(ctx, plan[2].logicalRank, static_cast<size_t>(14u));
    EXPECT_EQ(ctx, plan[2].targetLodIndex, kLod16);
}

void TestWindowLodCountsInvariantAcrossLap(TestContext& ctx)
{
    using namespace TrackStreamingPolicy;

    const std::array<size_t, 4> expectedCounts{{6u, 5u, 5u, 4u}};
    for (int32_t startId = 1; startId <= 305; ++startId)
    {
        const std::vector<int32_t> window = BuildWindowSegmentIds(startId, 305, 20u, +1);
        EXPECT_EQ(ctx, window.size(), static_cast<size_t>(20u));

        std::array<size_t, 4> counts{{0u, 0u, 0u, 0u}};
        for (size_t rank = 0; rank < window.size(); ++rank)
        {
            const uint8_t lod = ResolveLodIndexByRank(rank);
            if (lod < counts.size()) ++counts[lod];
        }
        EXPECT_EQ(ctx, counts, expectedCounts);
    }
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
        {"ResolveLodBandsForTwentySegmentWindow", &TestResolveLodBandsForTwentySegmentWindow},
        {"WindowSlidesForwardKeepingSameBandShape", &TestWindowSlidesForwardKeepingSameBandShape},
        {"WindowSlidesBackwardKeepingSameBandShape", &TestWindowSlidesBackwardKeepingSameBandShape},
        {"WindowWrapsAcrossLapBoundary", &TestWindowWrapsAcrossLapBoundary},
        {"CollectRetiredSlotsForRemovedFamilies", &TestCollectRetiredSlotsForRemovedFamilies},
        {"CollectRetiredSlotsDeduplicatesAcrossLods", &TestCollectRetiredSlotsDeduplicatesAcrossLods},
        {"RetiredSlotsIgnoreFamiliesStillVisible", &TestRetiredSlotsIgnoreFamiliesStillVisible},
        {"TopNearCameraRanksStayBoundedToFourSegments", &TestTopNearCameraRanksStayBoundedToFourSegments},
        {"ForwardSlideBoundaryPrewarmPlanMatchesContract", &TestForwardSlideBoundaryPrewarmPlanMatchesContract},
        {"WindowLodCountsInvariantAcrossLap", &TestWindowLodCountsInvariantAcrossLap},
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
