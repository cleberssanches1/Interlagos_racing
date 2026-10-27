#include <cassert>
#include <cstdint>

#include "auto_lap_distance.hpp"

int main()
{
    constexpr int32_t startX = 5304 * 65536;
    constexpr int32_t startZ = 1717 * 65536;
    constexpr int32_t nearX = 5066 * 65536;
    constexpr int32_t nearZ = 1755 * 65536;
    constexpr int32_t farX = 18000 * 65536;
    constexpr int32_t farZ = -7200 * 65536;
    const uint64_t nearScore = AutoLapDistance::ManhattanRaw(
        nearX, nearZ, startX, startZ);
    const uint64_t farScore = AutoLapDistance::ManhattanRaw(
        farX, farZ, startX, startZ);
    assert(nearScore == 276ull * 65536ull);
    assert(farScore > nearScore);
    assert(AutoLapDistance::ManhattanRaw(
        nearX, nearZ, startX, startZ) ==
        AutoLapDistance::ManhattanRaw(startX, startZ, nearX, nearZ));
    return 0;
}
