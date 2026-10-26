#pragma once

#include <cstdint>

namespace AutoLapDrivePolicy
{
struct Point
{
    int32_t xRaw = 0;
    int32_t zRaw = 0;
};

struct Input
{
    Point car{};
    Point current{};
    Point next{};
    Point afterNext{};
    int32_t forwardXRaw = 0; // Q16, same yaw convention as CarPhysics
    int32_t forwardZRaw = -(1 << 16);
    int16_t speedKmh = 0;
    int16_t upcomingTurnDeg = 0;
    uint8_t surfaceType = 0;
    bool groundSupported = false;
    bool wallHit = false;
};

struct State
{
    int16_t steering = 0;
    uint8_t missingGroundFrames = 0;
};

struct Output
{
    Point target{};
    int16_t targetSpeedKmh = 0;
    int16_t steering = 0;
    bool accelerate = false;
    bool brake = false;
};

inline int32_t Abs(int32_t value) { return value < 0 ? -value : value; }
inline int32_t Max(int32_t a, int32_t b) { return a > b ? a : b; }
inline int32_t Min(int32_t a, int32_t b) { return a < b ? a : b; }
inline int32_t Clamp(int32_t value, int32_t low, int32_t high)
{
    return Max(low, Min(high, value));
}

inline Point AlongSegment(const Point& from, const Point& to, int32_t distanceRaw)
{
    const int32_t dx = to.xRaw - from.xRaw;
    const int32_t dz = to.zRaw - from.zRaw;
    const int32_t length = Max(Abs(dx), Abs(dz));
    if (length <= 0) return to;
    const int32_t step = Clamp(distanceRaw, 0, length);
    return {
        from.xRaw + static_cast<int32_t>((static_cast<int64_t>(dx) * step) / length),
        from.zRaw + static_cast<int32_t>((static_cast<int64_t>(dz) * step) / length)
    };
}

inline Point LookaheadTarget(const Input& input)
{
    const int32_t dx = input.next.xRaw - input.current.xRaw;
    const int32_t dz = input.next.zRaw - input.current.zRaw;
    const int32_t length = Max(Abs(dx), Abs(dz));
    if (length <= 0) return input.next;

    // Project onto the dominant axis. This is bounded and cheaper than a
    // squared-length/square-root projection on every Saturn frame.
    const int32_t projected = (Abs(dx) >= Abs(dz))
        ? ((input.car.xRaw - input.current.xRaw) * (dx >= 0 ? 1 : -1))
        : ((input.car.zRaw - input.current.zRaw) * (dz >= 0 ? 1 : -1));
    const int32_t along = Clamp(projected, 0, length);
    const int32_t speed = Clamp(input.speedKmh, 0, 120);
    const int32_t lookahead = (55 + speed) << 16;
    if (along + lookahead <= length)
    {
        return AlongSegment(input.current, input.next, along + lookahead);
    }
    return AlongSegment(input.next, input.afterNext,
                        Min((along + lookahead) - length, 175 << 16));
}

inline int16_t TargetSpeedKmh(const Input& input, State& state)
{
    const int32_t turn = Abs(input.upcomingTurnDeg);
    int32_t target = (turn > 42) ? 34 :
                     (turn > 25) ? 44 :
                     (turn > 12) ? 56 :
                     (turn > 5) ? 68 : 82;
    if (input.surfaceType == 2u || input.surfaceType == 3u)
    {
        target = Min(target, 28);
    }
    if (input.groundSupported)
    {
        state.missingGroundFrames = 0;
    }
    else if (state.missingGroundFrames < 255u)
    {
        ++state.missingGroundFrames;
    }
    if (state.missingGroundFrames > 8u) target = Min(target, 22);
    if (input.wallHit) target = Min(target, 25);
    return static_cast<int16_t>(target);
}

inline Output Step(const Input& input, State& state)
{
    Output output{};
    output.target = LookaheadTarget(input);
    output.targetSpeedKmh = TargetSpeedKmh(input, state);

    const int32_t dx = output.target.xRaw - input.car.xRaw;
    const int32_t dz = output.target.zRaw - input.car.zRaw;
    const int32_t distance = Max(Abs(dx), Abs(dz));
    const int32_t cross = static_cast<int32_t>(
        ((static_cast<int64_t>(input.forwardXRaw) * dz) -
         (static_cast<int64_t>(input.forwardZRaw) * dx)) >> 16);
    int32_t wantedSteering = 0;
    if (distance > 0 && Abs(cross) > distance / 64)
    {
        // CarDynamicsModel applies yaw with the opposite sign to steering:
        // positive steering turns toward decreasing world X at yaw zero.
        // The XZ cross product has the target's world-side sign, so invert it
        // here rather than changing the player's established steering path.
        wantedSteering = -Clamp(static_cast<int32_t>((static_cast<int64_t>(cross) * 190) / distance),
                                -70, 70);
    }
    // Analog steering avoids holding the player's digital input at full lock.
    // The command state and the real wheel/physics path remain shared.
    state.steering = static_cast<int16_t>(Clamp(wantedSteering,
                                                state.steering - 12,
                                                state.steering + 12));
    output.steering = state.steering;

    const int32_t speed = Max(0, input.speedKmh);
    output.accelerate = speed < output.targetSpeedKmh - 5;
    output.brake = speed > output.targetSpeedKmh + 4 && speed > 18;
    if (state.missingGroundFrames > 8u)
    {
        // Do not drive into a persistent hole in TCOL support. Transient
        // contact misses still retain normal control during the grace window.
        output.accelerate = false;
    }
    if (input.wallHit && speed < 8)
    {
        output.accelerate = false;
        output.brake = false;
    }
    return output;
}
} // namespace AutoLapDrivePolicy
