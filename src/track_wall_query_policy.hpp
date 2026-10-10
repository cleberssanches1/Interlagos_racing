#pragma once

#include <cstdint>

namespace TrackWallQueryPolicy
{
// A lateral car hull may straddle a fence even though its endpoints are much
// farther than the probe radius from it. Move the *farther* endpoint back to
// the safe side; using radius alone leaves the body intersecting the fence.
constexpr int64_t HullCrossPenetrationRaw(int64_t firstSignedDistanceRaw,
                                          int64_t otherSignedDistanceRaw,
                                          int64_t safeSideSign,
                                          int64_t radiusRaw) noexcept
{
    if (safeSideSign == 0 || radiusRaw <= 0) return 0;
    const int64_t firstSafeDistance = firstSignedDistanceRaw * safeSideSign;
    const int64_t otherSafeDistance = otherSignedDistanceRaw * safeSideSign;
    const int64_t minimumSafeDistance =
        firstSafeDistance < otherSafeDistance ? firstSafeDistance : otherSafeDistance;
    const int64_t penetration = radiusRaw - minimumSafeDistance;
    return penetration > 0 ? penetration : 0;
}

// Project a point onto a finite XZ edge in 16.16 coordinates. Multiplying a
// raw edge vector by the raw dot product overflows int64 even for an ordinary
// 130-unit fence panel. Divide to a Q16 fraction before multiplying the edge.
// Keep one SH-2 copy: this routine is called from three probes inside a very
// large TrackSystem method, and inlining it there needlessly consumes HWR code.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
inline int64_t ClosestPointOnSegmentXZRaw(int64_t qx,
                                         int64_t qz,
                                         int64_t ax,
                                         int64_t az,
                                         int64_t bx,
                                         int64_t bz,
                                         int64_t& outCx,
                                         int64_t& outCz) noexcept
{
    const int64_t vx = bx - ax;
    const int64_t vz = bz - az;
    const int64_t lenSq = vx * vx + vz * vz;
    outCx = ax;
    outCz = az;
    if (lenSq > 0)
    {
        const int64_t dot = (qx - ax) * vx + (qz - az) * vz;
        if (dot >= lenSq)
        {
            outCx = bx;
            outCz = bz;
        }
        else if (dot > 0)
        {
            int64_t numerator = dot;
            int64_t denominator = lenSq;
            constexpr int64_t kMaxBeforeQ16Shift = 0x7fffffffffffffffLL >> 16;
            while (denominator > kMaxBeforeQ16Shift)
            {
                numerator >>= 1;
                denominator >>= 1;
            }
            const int64_t fractionQ16 = (numerator << 16) / denominator;
            outCx = ax + ((vx * fractionQ16) >> 16);
            outCz = az + ((vz * fractionQ16) >> 16);
        }
    }
    const int64_t dx = qx - outCx;
    const int64_t dz = qz - outCz;
    const int64_t absDx = dx < 0 ? -dx : dx;
    const int64_t absDz = dz < 0 ? -dz : dz;
    return absDx > absDz ? absDx : absDz;
}

// A wall miss is the normal case while the car is on the track, so it must not
// permanently enable the expensive recovery window. The bounded window is
// requested at a low cadence, or immediately when the current collision span
// no longer covers the car/hull sample.
constexpr bool IsCadencedRecoveryDue(bool hasSeedSegment,
                                     uint8_t cooldownFrames) noexcept
{
    return !hasSeedSegment || cooldownFrames == 0u;
}

constexpr uint8_t AdvanceRecoveryCooldown(uint8_t cooldownFrames,
                                          bool recoveryWasAttempted,
                                          uint8_t cadenceFrames) noexcept
{
    if (recoveryWasAttempted) return cadenceFrames;
    return cooldownFrames > 0u
        ? static_cast<uint8_t>(cooldownFrames - 1u)
        : 0u;
}

constexpr bool ShouldRunBoundedRecovery(bool localWallHit,
                                        bool cadenceRecoveryDue,
                                        bool localWindowCoversQuery) noexcept
{
    return !localWallHit &&
           (cadenceRecoveryDue || !localWindowCoversQuery);
}

// The 4x4 wall-cell grid is the ordinary broad phase. Once a swept sample has
// crossed outside the segment AABB, its closest wall is necessarily on the
// perimeter and its grid cell is no longer a reliable selector. In that rare
// transition, scan the segment's own wall list exactly once; never expand to
// the render window or full circuit.
constexpr bool ShouldScanPerimeterWalls(bool localWallHit,
                                        bool anySampleOutsideSegmentBounds) noexcept
{
    return !localWallHit && anySampleOutsideSegmentBounds;
}
} // namespace TrackWallQueryPolicy
