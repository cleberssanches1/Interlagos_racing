#pragma once

#include <cstdint>

namespace AutoLapStartupPolicy
{
inline uint32_t ManhattanWorldUnits(int32_t axRaw, int32_t azRaw,
                                    int32_t bxRaw, int32_t bzRaw)
{
    // A 16.16 coordinate fits in signed 16 world bits. After conversion,
    // either axis difference is at most 65535, so this sum cannot overflow.
    const int32_t dx = (axRaw >> 16) - (bxRaw >> 16);
    const int32_t dz = (azRaw >> 16) - (bzRaw >> 16);
    return static_cast<uint32_t>((dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz));
}

// PATH altitude is only a one-time spawn recovery, never a replacement for
// wheel support. Reject a missing TCOL map or a guide far from the spawn.
inline bool CanUsePathHeight(bool tcolReady, int32_t routeSegmentId,
                             uint16_t segmentCount, int64_t distanceSq)
{
    return tcolReady && routeSegmentId > 0 &&
           routeSegmentId <= static_cast<int32_t>(segmentCount) &&
           distanceSq >= 0 && distanceSq <= 1024ll * 1024ll;
}
} // namespace AutoLapStartupPolicy
