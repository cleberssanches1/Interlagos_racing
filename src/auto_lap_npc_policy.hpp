#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Independent, allocation-free progress for a car following a closed PATH line.
// Distances and speeds use the same signed 16.16 world units as PATH.NYA.
namespace AutoLapNpcPolicy
{
struct State
{
    uint16_t segment = 0u;
    int32_t alongRaw = 0;
    int32_t speedRaw = 0;
};

inline bool ShouldReverseLine(int32_t tangentYawDeg, int32_t playerYawDeg)
{
    int32_t delta = (tangentYawDeg - playerYawDeg) % 360;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
    return delta > 90 || delta < -90;
}

inline uint32_t TrackFaceBudget(uint32_t maxPolygons, uint32_t carFaces,
                                uint8_t npcCount)
{
    const uint32_t normalBudget = maxPolygons > 64u ? maxPolygons - 64u : maxPolygons;
    if (npcCount == 0u || carFaces == 0u) return normalBudget;
    const uint64_t reserved = static_cast<uint64_t>(carFaces) *
                              (1u + npcCount) + 64u;
    // Keep a usable track pass. The render gate will omit an NPC if a larger
    // future car model cannot fit this SGL work area.
    if (reserved + 400u >= maxPolygons) return normalBudget;
    return maxPolygons - static_cast<uint32_t>(reserved);
}

inline int32_t TargetSpeedRaw(int16_t turnDeg, uint8_t carIndex)
{
    const int32_t turn = turnDeg < 0 ? -static_cast<int32_t>(turnDeg) : turnDeg;
    const int32_t straight = carIndex == 0u ? 11 : 12;
    const int32_t units = turn > 28 ? 5 : turn > 14 ? 7 : turn > 6 ? 9 : straight;
    return units << 16;
}

inline void ApproachSpeed(State& state, int32_t targetRaw, uint8_t vblanks)
{
    const int32_t maxChangeRaw = static_cast<int32_t>(vblanks) << 14;
    state.speedRaw += std::clamp(targetRaw - state.speedRaw,
                                  -maxChangeRaw, maxChangeRaw);
}

inline uint8_t Advance(State& state, const int32_t* lengthsRaw,
                       size_t count, int32_t distanceRaw)
{
    if (!lengthsRaw || count < 2u || count > 65535u || distanceRaw <= 0) return 0u;
    uint8_t crossed = 0u;
    // The frame time is capped by the caller. This bound also protects a
    // malformed route with repeated points from looping indefinitely.
    for (uint8_t guard = 0u; guard < 16u; ++guard)
    {
        const size_t index = static_cast<size_t>(state.segment) % count;
        const int32_t lengthRaw = std::max<int32_t>(1, lengthsRaw[index]);
        const int32_t remainingRaw = lengthRaw - state.alongRaw;
        if (distanceRaw < remainingRaw)
        {
            state.alongRaw += distanceRaw;
            return crossed;
        }
        distanceRaw -= std::max<int32_t>(0, remainingRaw);
        state.segment = static_cast<uint16_t>((index + 1u) % count);
        state.alongRaw = 0;
        ++crossed;
        if (distanceRaw == 0) return crossed;
    }
    return crossed;
}
} // namespace AutoLapNpcPolicy
