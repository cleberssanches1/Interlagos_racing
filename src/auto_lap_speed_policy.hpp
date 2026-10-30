#pragma once

#include <cstddef>
#include <cstdint>

// PATH-based speed envelope. All distances are conservative XZ world units;
// the brake coefficient includes margin below the car's nominal 0.60
// world-units/frame^2. No square root or allocation is needed per frame.
namespace AutoLapSpeedPolicy
{
static constexpr int16_t kStraightMaxKmh = 335;
static constexpr int32_t kBrakeKmhSquaredPerWorldUnit = 24;

inline int32_t Abs(int32_t v) { return v < 0 ? -v : v; }
inline int32_t Max(int32_t a, int32_t b) { return a > b ? a : b; }
inline int32_t Min(int32_t a, int32_t b) { return a < b ? a : b; }

inline int32_t ShortestTurn(int32_t a, int32_t b)
{
    const int32_t delta = ((b - a + 540) % 360) - 180;
    return Abs(delta);
}

// Curvature is heading change per unit of PATH length, not just angle between
// two arbitrary waypoints. This keeps a densely sampled bend from looking flat.
inline int16_t CornerLimitKmh(int32_t turnDeg, int32_t spanUnits)
{
    if (turnDeg <= 0) return kStraightMaxKmh;
    const int32_t curvature = (turnDeg * 1000) / Max(60, spanUnits);
    return static_cast<int16_t>(
        curvature > 80 ? 40 : curvature > 50 ? 55 :
        curvature > 25 ? 85 : curvature > 12 ? 130 :
        curvature > 5 ? 190 : 260);
}

inline int16_t SpeedFromEnvelope(int32_t maxSpeedSquared)
{
    int32_t low = 0;
    int32_t high = kStraightMaxKmh;
    while (low < high)
    {
        const int32_t middle = (low + high + 1) >> 1;
        if (middle * middle <= maxSpeedSquared) low = middle;
        else high = middle - 1;
    }
    return static_cast<int16_t>(low);
}

inline int16_t BrakeApproachSpeed(int16_t targetKmh, int32_t distanceUnits)
{
    const int32_t target = Max(0, Min(kStraightMaxKmh, targetKmh));
    const int32_t distance = Max(0, Min(6000, distanceUnits));
    return SpeedFromEnvelope(target * target +
                             kBrakeKmhSquaredPerWorldUnit * distance);
}

// Works on both the player's retained PATH route and the NPC's LWR route.
// At most 32 waypoints and 6000 world units are inspected per car/frame.
template <typename Route>
inline int16_t RouteSpeed(const Route& route, size_t current,
                          int32_t carXRaw, int32_t carZRaw)
{
    const size_t count = route.centers.size();
    if (count < 3u || route.yawDeg.size() != count) return 82;
    current %= count;
    size_t next = (current + 1u) % count;
    int32_t dx = (route.centers[next].X.RawValue() - carXRaw) >> 16;
    int32_t dz = (route.centers[next].Z.RawValue() - carZRaw) >> 16;
    int32_t distance = Max(Abs(dx), Abs(dz));
    int32_t envelope = kStraightMaxKmh * kStraightMaxKmh;
    size_t previous = current;
    for (size_t step = 0u; step < 32u && step < count && distance < 6000;
         ++step)
    {
        const size_t after = (next + 1u) % count;
        dx = (route.centers[after].X.RawValue() -
              route.centers[next].X.RawValue()) >> 16;
        dz = (route.centers[after].Z.RawValue() -
              route.centers[next].Z.RawValue()) >> 16;
        const int32_t span = Max(1, Max(Abs(dx), Abs(dz)));
        const int32_t turn = ShortestTurn(route.yawDeg[previous],
                                           route.yawDeg[next]);
        const int32_t corner = CornerLimitKmh(turn, span);
        envelope = Min(envelope, corner * corner +
                       kBrakeKmhSquaredPerWorldUnit * distance);
        distance += span;
        previous = next;
        next = after;
    }
    // Dense routes can exhaust the fixed scan before the full braking horizon.
    // Do not authorize top speed beyond the part of PATH that was inspected.
    if (distance < 5000)
    {
        const int32_t visibleCap = BrakeApproachSpeed(82, distance);
        envelope = Min(envelope, visibleCap * visibleCap);
    }
    return SpeedFromEnvelope(envelope);
}

template <typename Route>
inline void RouteLookahead(const Route& route, size_t current,
                           int32_t carXRaw, int32_t carZRaw,
                           int16_t speedKmh, int32_t& outXRaw,
                           int32_t& outZRaw)
{
    const size_t count = route.centers.size();
    if (count < 2u) return;
    current %= count;
    const int32_t wanted = Max(150, Min(750, 90 + speedKmh * 2));
    size_t from = current;
    int32_t remaining = wanted;
    for (size_t step = 0u; step < 12u && step < count; ++step)
    {
        const size_t to = (from + 1u) % count;
        const int32_t fromX = step == 0u ? carXRaw : route.centers[from].X.RawValue();
        const int32_t fromZ = step == 0u ? carZRaw : route.centers[from].Z.RawValue();
        const int32_t dx = route.centers[to].X.RawValue() - fromX;
        const int32_t dz = route.centers[to].Z.RawValue() - fromZ;
        const int32_t span = Max(1, Max(Abs(dx >> 16), Abs(dz >> 16)));
        if (remaining <= span)
        {
            outXRaw = fromX + static_cast<int32_t>(
                (static_cast<int64_t>(dx) * remaining) / span);
            outZRaw = fromZ + static_cast<int32_t>(
                (static_cast<int64_t>(dz) * remaining) / span);
            return;
        }
        remaining -= span;
        from = to;
    }
    outXRaw = route.centers[from].X.RawValue();
    outZRaw = route.centers[from].Z.RawValue();
}
} // namespace AutoLapSpeedPolicy
