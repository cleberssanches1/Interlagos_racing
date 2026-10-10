#pragma once

#include <cstdint>

namespace Game::CarPhysics::WallResponsePolicy
{
// Pick the longitudinal edge that leads the motion. The low-cost Saturn wall
// query keeps one lateral hull segment, so moving that segment to the leading
// axle catches the barrier before the car centre crosses it.
constexpr int32_t ResolveLeadingHullOffsetRaw(int32_t forwardSpeedRaw,
                                              int32_t halfLengthRaw) noexcept
{
    if (halfLengthRaw < 0) halfLengthRaw = -halfLengthRaw;
    return (forwardSpeedRaw < 0) ? -halfLengthRaw : halfLengthRaw;
}

// Restitution is expressed as 16.16. The returned speed is always outward,
// proportional to the inward impact speed, and bounded for predictable arcade
// behaviour on the SH-2 fixed step.
constexpr int32_t ResolveOutwardBounceRaw(int32_t inwardImpactSpeedRaw,
                                          int32_t restitutionRaw,
                                          int32_t minimumBounceRaw,
                                          int32_t maximumBounceRaw) noexcept
{
    if (inwardImpactSpeedRaw <= 0 || restitutionRaw <= 0 ||
        maximumBounceRaw <= 0)
    {
        return 0;
    }
    if (minimumBounceRaw < 0) minimumBounceRaw = 0;
    if (minimumBounceRaw > maximumBounceRaw) minimumBounceRaw = maximumBounceRaw;

    int64_t bounceRaw =
        (static_cast<int64_t>(inwardImpactSpeedRaw) * restitutionRaw) >> 16;
    if (bounceRaw < minimumBounceRaw) bounceRaw = minimumBounceRaw;
    if (bounceRaw > maximumBounceRaw) bounceRaw = maximumBounceRaw;
    return static_cast<int32_t>(bounceRaw);
}
} // namespace Game::CarPhysics::WallResponsePolicy
