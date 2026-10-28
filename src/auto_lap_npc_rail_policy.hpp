#pragma once

#include <cstddef>
#include <cstdint>

// Side PATH.NYA rails used by Auto Lap NPC pose-followers.
// Player Auto Lap keeps the middle guide line (index 1).
// mesh3: one NPC only (VDP1 poly budget) on left guide 0, same Auto Lap mold.
namespace AutoLapNpcRailPolicy
{
constexpr size_t kNpcRailCount = 1u;
constexpr int32_t kLeftGuideLineIndex = 0;
constexpr int32_t kRightGuideLineIndex = 2;
constexpr int32_t kNpcGuideLineIndices[kNpcRailCount] = {
    kLeftGuideLineIndex};

inline int32_t GuideLineIndexForNpcSlot(size_t slot)
{
    if (slot >= kNpcRailCount) return -1;
    return kNpcGuideLineIndices[slot];
}

inline int32_t NpcSlotForGuideLineIndex(int32_t lineIndex)
{
    for (size_t slot = 0; slot < kNpcRailCount; ++slot)
    {
        if (kNpcGuideLineIndices[slot] == lineIndex)
        {
            return static_cast<int32_t>(slot);
        }
    }
    return -1;
}

// Guard against the prior NPC attempt where route point[0] arrived as (0,0).
inline bool IsNonZeroRouteXZ(int32_t xRaw, int32_t zRaw)
{
    return xRaw != 0 || zRaw != 0;
}

// InitializeAutoLapPhysicalStart failure stages (HUD: AUTO START fail:N).
namespace StartFail
{
constexpr uint8_t kOk = 0u;
constexpr uint8_t kCarOrTrack = 1u;
constexpr uint8_t kRouteReady = 2u;
constexpr uint8_t kRouteData = 3u;
constexpr uint8_t kNearestPoint = 4u;
constexpr uint8_t kAsphaltTcol = 5u;
} // namespace StartFail
} // namespace AutoLapNpcRailPolicy
