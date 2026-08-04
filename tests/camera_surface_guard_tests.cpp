#include <cstdint>
#include <iostream>

#include "camera_surface_guard.hpp"

namespace
{
int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { \
    std::cerr << __LINE__ << ": expected " #expr "\n"; ++failures; } } while (0)

void TestQueriesEveryOtherFrame()
{
    CameraSurfaceGuardState state{};
    EXPECT_TRUE(CameraSurfaceGuard::ShouldQuery(state));
    CameraSurfaceGuard::Observe(state, 100 << 16, 30, 4);
    EXPECT_TRUE(!CameraSurfaceGuard::ShouldQuery(state));
    EXPECT_TRUE(CameraSurfaceGuard::ShouldQuery(state));
}

void TestPenetrationIsClampedAboveRoad()
{
    CameraSurfaceGuardState state{};
    CameraSurfaceGuard::Observe(state, 100 << 16, 30, 4);
    const int32_t resolved = CameraSurfaceGuard::ResolveYRaw(state, 90 << 16);
    EXPECT_TRUE(resolved == ((100 - 24) << 16));
}

void TestAlreadySafeCameraIsUntouched()
{
    CameraSurfaceGuardState state{};
    CameraSurfaceGuard::Observe(state, 100 << 16, 30, 4);
    const int32_t desired = 60 << 16;
    EXPECT_TRUE(CameraSurfaceGuard::ResolveYRaw(state, desired) == desired);
}

void TestOneMissHoldsConstraintAndSecondReleases()
{
    CameraSurfaceGuardState state{};
    CameraSurfaceGuard::Observe(state, 100 << 16, 30, 4);
    CameraSurfaceGuard::Miss(state);
    EXPECT_TRUE(state.valid);
    CameraSurfaceGuard::Miss(state);
    EXPECT_TRUE(!state.valid);
    EXPECT_TRUE(CameraSurfaceGuard::ResolveYRaw(state, 90 << 16) == (90 << 16));
}

void TestDescendingSurfaceReleasesEveryFrame()
{
    CameraSurfaceGuardState state{};
    CameraSurfaceGuard::Observe(state, 100 << 16, 30, 4);
    CameraSurfaceGuard::Observe(state, 140 << 16, 31, 7);
    const int32_t first = CameraSurfaceGuard::ResolveYRaw(state, 140 << 16);
    const int32_t second = CameraSurfaceGuard::ResolveYRaw(state, 140 << 16);
    EXPECT_TRUE(first == ((120 - 24) << 16));
    EXPECT_TRUE(second == ((140 - 24) << 16));
}

void TestImplausibleDescendingHitRemainsCapped()
{
    CameraSurfaceGuardState state{};
    CameraSurfaceGuard::Observe(state, 100 << 16, 30, 4);
    CameraSurfaceGuard::Observe(state, 200 << 16, 31, 7);
    const int32_t first = CameraSurfaceGuard::ResolveYRaw(state, 200 << 16);
    const int32_t second = CameraSurfaceGuard::ResolveYRaw(state, 200 << 16);
    EXPECT_TRUE(first == ((124 - 24) << 16));
    EXPECT_TRUE(second == ((148 - 24) << 16));
}

void TestRisingSurfaceTightensImmediately()
{
    CameraSurfaceGuardState state{};
    CameraSurfaceGuard::Observe(state, 100 << 16, 30, 4);
    CameraSurfaceGuard::Observe(state, 60 << 16, 29, 2);
    EXPECT_TRUE(CameraSurfaceGuard::ResolveYRaw(state, 70 << 16) ==
                ((60 - 24) << 16));
}
} // namespace

int main()
{
    TestQueriesEveryOtherFrame();
    TestPenetrationIsClampedAboveRoad();
    TestAlreadySafeCameraIsUntouched();
    TestOneMissHoldsConstraintAndSecondReleases();
    TestDescendingSurfaceReleasesEveryFrame();
    TestImplausibleDescendingHitRemainsCapped();
    TestRisingSurfaceTightensImmediately();
    if (failures != 0)
    {
        std::cerr << "camera_surface_guard_tests: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "camera_surface_guard_tests: PASS\n";
    return 0;
}
