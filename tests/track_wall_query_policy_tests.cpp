#include <iostream>

#include "track_wall_query_policy.hpp"

namespace
{
int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { std::cerr << __LINE__ << ": expected true: " #expr "\n"; ++failures; } \
} while (0)

#define EXPECT_FALSE(expr) do { \
    if ((expr)) { std::cerr << __LINE__ << ": expected false: " #expr "\n"; ++failures; } \
} while (0)

#define EXPECT_EQ(actual, expected) do { \
    const auto actualValue = (actual); \
    const auto expectedValue = (expected); \
    if (actualValue != expectedValue) { \
        std::cerr << __LINE__ << ": expected " << expectedValue << ", got " << actualValue << "\n"; \
        ++failures; \
    } \
} while (0)

void TestCadenceDoesNotTurnEveryNormalMissIntoWideScan()
{
    using namespace TrackWallQueryPolicy;
    EXPECT_TRUE(IsCadencedRecoveryDue(true, 0u));
    EXPECT_FALSE(IsCadencedRecoveryDue(true, 8u));
    EXPECT_TRUE(IsCadencedRecoveryDue(false, 8u));
    EXPECT_FALSE(ShouldRunBoundedRecovery(false, false, true));
    EXPECT_TRUE(ShouldRunBoundedRecovery(false, true, true));
    EXPECT_FALSE(ShouldRunBoundedRecovery(true, true, false));
}

void TestOutOfWindowRecoveryIsImmediate()
{
    using namespace TrackWallQueryPolicy;
    EXPECT_TRUE(ShouldRunBoundedRecovery(false, false, false));
    EXPECT_EQ(AdvanceRecoveryCooldown(0u, true, 8u), 8u);
    EXPECT_EQ(AdvanceRecoveryCooldown(8u, false, 8u), 7u);
    EXPECT_EQ(AdvanceRecoveryCooldown(0u, false, 8u), 0u);
}

void TestPerimeterFallbackOnlyRunsAfterTheAabbTransition()
{
    using namespace TrackWallQueryPolicy;
    EXPECT_FALSE(ShouldScanPerimeterWalls(false, false));
    EXPECT_TRUE(ShouldScanPerimeterWalls(false, true));
    EXPECT_FALSE(ShouldScanPerimeterWalls(true, true));
}

void TestProjectionOnLongCircuitWallDoesNotOverflow()
{
    using namespace TrackWallQueryPolicy;
    constexpr int64_t unit = 65536;
    // Segment 3 has ~128-unit fence spans near X=5003, Z=1307..1179.
    const int64_t ax = 5003 * unit;
    const int64_t az = 1307 * unit;
    const int64_t bx = 5005 * unit;
    const int64_t bz = 1179 * unit;
    int64_t cx = 0;
    int64_t cz = 0;
    const int64_t distance = ClosestPointOnSegmentXZRaw(
        5006 * unit, 1243 * unit, ax, az, bx, bz, cx, cz);
    EXPECT_TRUE(cx >= 5003 * unit && cx <= 5005 * unit);
    EXPECT_TRUE(cz >= 1242 * unit && cz <= 1244 * unit);
    EXPECT_TRUE(distance >= 1 * unit && distance <= 3 * unit);

    // Both endpoints and a tiny/degenerate edge are valid too.
    EXPECT_EQ(ClosestPointOnSegmentXZRaw(5003 * unit, 1317 * unit,
                                        ax, az, bx, bz, cx, cz), 10 * unit);
    EXPECT_EQ(cz, az);
    EXPECT_EQ(ClosestPointOnSegmentXZRaw(5003 * unit, 1317 * unit,
                                        ax, az, ax, az, cx, cz), 10 * unit);
    EXPECT_EQ(cx, ax);
    EXPECT_EQ(cz, az);
}

void TestProjectionOnCarHullAtWallContact()
{
    using namespace TrackWallQueryPolicy;
    constexpr int64_t unit = 65536;
    int64_t cx = 0;
    int64_t cz = 0;
    const int64_t distance = ClosestPointOnSegmentXZRaw(
        5004 * unit, 1243 * unit,
        5024 * unit, 1243 * unit,
        4980 * unit, 1243 * unit,
        cx, cz);
    EXPECT_TRUE(cx >= 5003 * unit && cx <= 5005 * unit);
    EXPECT_EQ(cz, 1243 * unit);
    EXPECT_TRUE(distance <= 128); // Q16 projection truncation (< 0.002 unit)
}

void TestProjectionOnVeryLongSegmentUsesScaledFraction()
{
    using namespace TrackWallQueryPolicy;
    constexpr int64_t unit = 65536;
    int64_t cx = 0;
    int64_t cz = 0;
    const int64_t distance = ClosestPointOnSegmentXZRaw(
        5 * unit, 512 * unit,
        0, 0,
        0, 1024 * unit,
        cx, cz);
    EXPECT_EQ(cx, 0);
    EXPECT_EQ(cz, 512 * unit);
    EXPECT_EQ(distance, 5 * unit);
}

void TestLateralHullCrossingSeparatesBothSides()
{
    using TrackWallQueryPolicy::HullCrossPenetrationRaw;
    constexpr int64_t unit = 65536;
    // The old crossing response was just the 2-unit probe radius. A 20-unit
    // penetration of the opposite side requires 22 units of displacement.
    EXPECT_EQ(HullCrossPenetrationRaw(-20 * unit, 20 * unit, 1, 2 * unit),
              22 * unit);
    EXPECT_EQ(HullCrossPenetrationRaw(-20 * unit, 20 * unit, -1, 2 * unit),
              22 * unit);
    EXPECT_EQ(HullCrossPenetrationRaw(3 * unit, 25 * unit, 1, 2 * unit), 0);
    EXPECT_EQ(HullCrossPenetrationRaw(1 * unit, 25 * unit, 1, 2 * unit), unit);
}
} // namespace

int main()
{
    TestCadenceDoesNotTurnEveryNormalMissIntoWideScan();
    TestOutOfWindowRecoveryIsImmediate();
    TestPerimeterFallbackOnlyRunsAfterTheAabbTransition();
    TestProjectionOnLongCircuitWallDoesNotOverflow();
    TestProjectionOnCarHullAtWallContact();
    TestProjectionOnVeryLongSegmentUsesScaledFraction();
    TestLateralHullCrossingSeparatesBothSides();
    if (failures != 0)
    {
        std::cerr << "track_wall_query_policy_tests: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "track_wall_query_policy_tests: PASS\n";
    return 0;
}
