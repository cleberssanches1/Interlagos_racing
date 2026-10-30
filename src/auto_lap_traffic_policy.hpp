#pragma once

#include <cstdint>

#include "auto_lap_drive_policy.hpp"
#include "auto_lap_speed_policy.hpp"

// One opponent, world XZ Q16.16. The rear car yields only when the other
// car is ahead inside the same driving corridor; no path or heap queries.
namespace AutoLapTrafficPolicy
{
struct Vehicle
{
    int32_t xRaw = 0;
    int32_t zRaw = 0;
    int32_t forwardXRaw = 0;
    int32_t forwardZRaw = -(1 << 16);
    int16_t speedKmh = 0;
};

struct State
{
    int16_t heldLimitKmh = 0;
    uint8_t holdFrames = 0;
};

struct PassState
{
    int8_t side = 0; // -1 = left of PATH, +1 = right of PATH
    uint8_t checkCountdown = 0;
    uint8_t cooldown = 0;
    uint16_t activeFrames = 0;
};

struct RelativePosition
{
    int32_t aheadRaw = 0;
    int32_t sideRaw = 0;
};

inline RelativePosition RelativeTo(const Vehicle& ego, const Vehicle& other)
{
    const int64_t dx = static_cast<int64_t>(other.xRaw) - ego.xRaw;
    const int64_t dz = static_cast<int64_t>(other.zRaw) - ego.zRaw;
    return {
        static_cast<int32_t>((dx * ego.forwardXRaw + dz * ego.forwardZRaw) >> 16),
        static_cast<int32_t>((-dx * ego.forwardZRaw + dz * ego.forwardXRaw) >> 16)};
}

inline bool StoppedObstacleAhead(const Vehicle& ego, const Vehicle& other)
{
    if (other.speedKmh > 5 || other.speedKmh < -5) return false;
    const RelativePosition relative = RelativeTo(ego, other);
    return relative.aheadRaw > 0 && relative.aheadRaw < (300 << 16) &&
           relative.sideRaw > -(90 << 16) && relative.sideRaw < (90 << 16);
}

inline bool PassedObstacle(const Vehicle& ego, const Vehicle& other)
{
    return RelativeTo(ego, other).aheadRaw < -(120 << 16);
}

inline int8_t PreferredPassSide(const Vehicle& ego, const Vehicle& other)
{
    // Move away from the obstacle; center-on-center prefers the right.
    return RelativeTo(ego, other).sideRaw > 0 ? -1 : 1;
}

inline AutoLapDrivePolicy::Point OffsetTarget(
    AutoLapDrivePolicy::Point routeTarget,
    int32_t routeForwardXRaw, int32_t routeForwardZRaw,
    int8_t side, int32_t offsetRaw = 90 << 16)
{
    routeTarget.xRaw += static_cast<int32_t>(
        (-static_cast<int64_t>(routeForwardZRaw) * side * offsetRaw) >> 16);
    routeTarget.zRaw += static_cast<int32_t>(
        (static_cast<int64_t>(routeForwardXRaw) * side * offsetRaw) >> 16);
    return routeTarget;
}

struct StallState
{
    int32_t lastXRaw = 0;
    int32_t lastZRaw = 0;
    uint8_t stoppedFrames = 0;
    bool initialized = false;
};

inline bool TickStall(StallState& state, int32_t xRaw, int32_t zRaw,
                      int16_t speedKmh, bool autoLapActive)
{
    if (!state.initialized)
    {
        state.lastXRaw = xRaw;
        state.lastZRaw = zRaw;
        state.initialized = true;
        return false;
    }
    const int64_t dx = static_cast<int64_t>(xRaw) - state.lastXRaw;
    const int64_t dz = static_cast<int64_t>(zRaw) - state.lastZRaw;
    state.lastXRaw = xRaw;
    state.lastZRaw = zRaw;
    const bool noProgress = dx > -(1 << 15) && dx < (1 << 15) &&
                            dz > -(1 << 15) && dz < (1 << 15);
    // A traffic limit of zero also means no throttle; that must not hide a
    // car which has made no progress for several seconds.
    // autolap49: 90 frames (~3s) left the player in the void too long after
    // GS=0 emptied the belt. Recover sooner once progress is clearly dead.
    if (autoLapActive && speedKmh < 4 && speedKmh > -4 && noProgress)
    {
        if (state.stoppedFrames < 45u) ++state.stoppedFrames;
    }
    else state.stoppedFrames = 0u;
    return state.stoppedFrames >= 45u;
}

// NPC recovery uses published XZ progress, not drivetrain speed. A wheel can
// spin against a wall while the car's world position remains unchanged.
struct NpcProgressState
{
    int32_t anchorXRaw = 0;
    int32_t anchorZRaw = 0;
    uint8_t stoppedFrames = 0u;
    uint8_t missingGroundFrames = 0u;
    bool initialized = false;
};

inline bool TickNpcRecovery(NpcProgressState& state, int32_t xRaw, int32_t zRaw,
                            bool groundSupported, bool blockedByTraffic)
{
    if (!state.initialized)
    {
        state.anchorXRaw = xRaw;
        state.anchorZRaw = zRaw;
        state.initialized = true;
    }
    const int64_t dx = static_cast<int64_t>(xRaw) - state.anchorXRaw;
    const int64_t dz = static_cast<int64_t>(zRaw) - state.anchorZRaw;
    const bool progressed = dx <= -(2LL << 16) || dx >= (2LL << 16) ||
                            dz <= -(2LL << 16) || dz >= (2LL << 16);
    if (progressed || blockedByTraffic)
    {
        state.anchorXRaw = xRaw;
        state.anchorZRaw = zRaw;
        state.stoppedFrames = 0u;
    }
    else if (state.stoppedFrames < 90u)
    {
        ++state.stoppedFrames;
    }
    // autolap46: 24 frames left the NPC frozen too long with GS=0.
    // Trigger rail recovery / RestoreSafePose sooner (still > accel-cut at 8).
    if (groundSupported)
        state.missingGroundFrames = 0u;
    else if (state.missingGroundFrames < 12u)
        ++state.missingGroundFrames;
    return state.stoppedFrames >= 90u || state.missingGroundFrames >= 12u;
}

inline int32_t Abs(int32_t v) { return v < 0 ? -v : v; }

inline int16_t LimitSpeed(const Vehicle& ego, const Vehicle& other,
                          int16_t routeLimitKmh, State& state)
{
    // At 335 km/h, the nominal 400-unit scan cannot see a stopped car early
    // enough to brake. The same fixed-point envelope as the corners gives a
    // bounded, speed-dependent approach without extra track queries.
    constexpr int64_t kRangeRaw = 6000LL << 16;
    const int64_t dx = static_cast<int64_t>(other.xRaw) - ego.xRaw;
    const int64_t dz = static_cast<int64_t>(other.zRaw) - ego.zRaw;
    bool threat = false;
    int16_t limit = routeLimitKmh;
    if (dx >= -kRangeRaw && dx <= kRangeRaw &&
        dz >= -kRangeRaw && dz <= kRangeRaw)
    {
        const int64_t aheadRaw =
            (dx * ego.forwardXRaw + dz * ego.forwardZRaw) >> 16;
        const int64_t sideRaw =
            (-dx * ego.forwardZRaw + dz * ego.forwardXRaw) >> 16;
        const int64_t headingDot =
            (static_cast<int64_t>(ego.forwardXRaw) * other.forwardXRaw +
             static_cast<int64_t>(ego.forwardZRaw) * other.forwardZRaw) >> 16;
        if (aheadRaw > 0 && aheadRaw < kRangeRaw &&
            sideRaw > -(52LL << 16) && sideRaw < (52LL << 16) &&
            headingDot > 46340)
        {
            const int32_t otherSpeed = other.speedKmh > 0 ? other.speedKmh : 0;
            const int32_t gap = static_cast<int32_t>(aheadRaw >> 16);
            const int32_t brakingGap = gap > 90 ? gap - 90 : 0;
            int32_t wanted = AutoLapSpeedPolicy::BrakeApproachSpeed(
                static_cast<int16_t>(otherSpeed), brakingGap);
            if (gap < 150)
            {
                const int32_t closeLimit = otherSpeed - 4 - (150 - gap) / 6;
                if (closeLimit < wanted) wanted = closeLimit;
                if (wanted < 0) wanted = 0;
            }
            if (gap < 55) wanted = 0;
            if (wanted < limit)
            {
                threat = true;
                limit = static_cast<int16_t>(wanted);
            }
        }
    }
    if (threat)
    {
        state.heldLimitKmh = limit;
        state.holdFrames = 4u;
    }
    else if (state.holdFrames > 0u)
    {
        --state.holdFrames;
        if (state.heldLimitKmh < limit) limit = state.heldLimitKmh;
    }
    return limit;
}

inline void ApplySpeedLimit(AutoLapDrivePolicy::Output& command,
                            int16_t speedKmh, int16_t limitKmh)
{
    if (limitKmh >= command.targetSpeedKmh) return;
    command.targetSpeedKmh = limitKmh;
    command.accelerate = speedKmh < limitKmh - 5;
    command.brake = speedKmh > limitKmh + 4 && speedKmh > 3;
}
} // namespace AutoLapTrafficPolicy
