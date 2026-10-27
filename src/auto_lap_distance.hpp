#pragma once

#include <cstdint>

namespace AutoLapDistance
{
// Keep PATH comparisons in raw 16.16 units without wrapping at 32768 world units.
constexpr uint64_t ManhattanRaw(int32_t ax, int32_t az, int32_t bx, int32_t bz)
{
    const int64_t dx = static_cast<int64_t>(ax) - bx;
    const int64_t dz = static_cast<int64_t>(az) - bz;
    return static_cast<uint64_t>(dx < 0 ? -dx : dx) +
           static_cast<uint64_t>(dz < 0 ? -dz : dz);
}
} // namespace AutoLapDistance
