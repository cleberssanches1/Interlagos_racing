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

// The Master may render several frames while the Slave is still computing one
// player physics frame. NPC physics must consume that result at most once.
inline bool ShouldAdvanceForSimulationFrame(bool hasPublishedFrame,
                                            uint32_t publishedFrameId,
                                            bool hasConsumedFrame,
                                            uint32_t consumedFrameId)
{
    return hasPublishedFrame &&
           (!hasConsumedFrame || publishedFrameId != consumedFrameId);
}

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

// Keep an independently moving NPC near the player's streamed track window.
// A small station error changes speed, never position or PATH pointIndex.
inline int16_t StepUnitsForNpc(int16_t playerSpeedKmh, int32_t stationGap)
{
    int32_t step = (playerSpeedKmh > 0)
        ? (static_cast<int32_t>(playerSpeedKmh) * 100) / 550
        : 0;
    int32_t gapError = 1 - stationGap; // target: roughly one PATH point ahead
    if (gapError < -4) gapError = -4;
    if (gapError > 4) gapError = 4;
    step += gapError * 2;
    if (step < 0) step = 0;
    if (step > 22) step = 22;
    return static_cast<int16_t>(step);
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
