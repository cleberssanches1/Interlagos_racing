#include <cstdint>
#include <iostream>

#include "car_wall_response.hpp"

namespace
{
int failures = 0;

#define EXPECT_EQ(actual, expected) do { \
    const auto actualValue = (actual); \
    const auto expectedValue = (expected); \
    if (actualValue != expectedValue) { \
        std::cerr << __LINE__ << ": expected " << expectedValue \
                  << ", got " << actualValue << "\n"; \
        ++failures; \
    } \
} while (0)

void TestLeadingHullTracksDirection()
{
    constexpr int32_t halfLength = 37 << 16;
    EXPECT_EQ(Game::CarPhysics::WallResponsePolicy::ResolveLeadingHullOffsetRaw(
                  4 << 16, halfLength),
              halfLength);
    EXPECT_EQ(Game::CarPhysics::WallResponsePolicy::ResolveLeadingHullOffsetRaw(
                  -(4 << 16), halfLength),
              -halfLength);
    EXPECT_EQ(Game::CarPhysics::WallResponsePolicy::ResolveLeadingHullOffsetRaw(
                  0, -halfLength),
              halfLength);
}

void TestBounceIsSpeedScaledAndLimited()
{
    constexpr int32_t restitution = 1 << 14; // 0.25
    constexpr int32_t minimum = 1 << 16;
    constexpr int32_t maximum = 8 << 16;
    using Game::CarPhysics::WallResponsePolicy::ResolveOutwardBounceRaw;

    EXPECT_EQ(ResolveOutwardBounceRaw(0, restitution, minimum, maximum), 0);
    EXPECT_EQ(ResolveOutwardBounceRaw(2 << 16, restitution, minimum, maximum), minimum);
    EXPECT_EQ(ResolveOutwardBounceRaw(20 << 16, restitution, minimum, maximum), 5 << 16);
    EXPECT_EQ(ResolveOutwardBounceRaw(100 << 16, restitution, minimum, maximum), maximum);
}
} // namespace

int main()
{
    TestLeadingHullTracksDirection();
    TestBounceIsSpeedScaledAndLimited();
    if (failures != 0)
    {
        std::cerr << "car_wall_response_tests: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "car_wall_response_tests: PASS\n";
    return 0;
}
