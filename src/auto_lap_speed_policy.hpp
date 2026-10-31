#pragma once

#include <cstddef>
#include <cstdint>

// PATH-based speed envelope + cheap racing-line offset. All distances are
// conservative XZ world units; the brake coefficient includes margin below the
// car's nominal 0.60 world-units/frame^2. No square root or allocation per frame.
namespace AutoLapSpeedPolicy
{
static constexpr int16_t kStraightMaxKmh = 335;
// C2: slightly later braking than the original 24 (still below true decel).
static constexpr int32_t kBrakeKmhSquaredPerWorldUnit = 28;
// C2: when the fixed scan ends early, do not clamp as if a hairpin is next.
static constexpr int16_t kVisibleScanCapKmh = 110;

// C1: apex-biased lateral offset from mid-rail (arcade out is deferred to C1.1).
static constexpr int32_t kRacingLineLatPerDeg = 1;
static constexpr int32_t kRacingLineMaxLatUnits = 48;
static constexpr int32_t kRacingLineMinTurnDeg = 4;
// Flip to -1 if a left/right Interlagos corner cuts the wrong way.
static constexpr int32_t kRacingLineSign = 1;

inline int32_t Abs(int32_t v) { return v < 0 ? -v : v; }
inline int32_t Max(int32_t a, int32_t b) { return a > b ? a : b; }
inline int32_t Min(int32_t a, int32_t b) { return a < b ? a : b; }
inline int32_t Clamp(int32_t value, int32_t low, int32_t high)
{
    return Max(low, Min(high, value));
}

inline int32_t ShortestTurn(int32_t a, int32_t b)
{
    const int32_t delta = ((b - a + 540) % 360) - 180;
    return Abs(delta);
}

inline int32_t ShortestTurnSigned(int32_t a, int32_t b)
{
    return ((b - a + 540) % 360) - 180;
}

// Inclusive segment ranges. Base targets were the Norris 2025 pole, then
// scaled from driving notes. "Acelerar" is +10%, clamped at kStraightMaxKmh.
// 41 stays with Curva do Sol. 142 stays with Laranjinha. 269 stays on the
// straight so the racing line does not brake at pit entry.
struct SectionSpeed
{
    int16_t first;
    int16_t last;
    int16_t kmh;
};

static constexpr SectionSpeed kSectionSpeeds[] = {
    {1, 26, 335},
    {27, 35, 128},
    {36, 38, 60},
    {39, 40, 60},
    {41, 61, 262},
    {62, 62, 308},
    {63, 63, 335},
    {64, 90, 332},
    {91, 98, 100},
    {99, 109, 230},
    {110, 113, 170},
    {114, 117, 206},
    {118, 141, 210},
    {142, 159, 180},
    {160, 165, 110},
    {166, 174, 90},
    {175, 182, 120},
    {183, 189, 160},
    {190, 197, 50},
    {198, 202, 180},
    {203, 212, 260},
    {213, 223, 250},
    {224, 229, 90},
    {230, 240, 180},
    {241, 253, 230},
    {254, 290, 335},
};

inline int16_t SectionTargetKmh(int32_t segmentId)
{
    if (segmentId <= 0) return 0;
    for (const SectionSpeed& row : kSectionSpeeds)
    {
        if (segmentId < row.first) return 0;
        if (segmentId <= row.last) return row.kmh;
    }
    return 0;
}

template <typename Route>
inline auto RouteSegmentId(const Route& route, size_t index, int)
    -> decltype(static_cast<int16_t>(route.ids[index]))
{
    if (route.ids.size() != route.centers.size() || index >= route.ids.size())
        return 0;
    return static_cast<int16_t>(route.ids[index]);
}

template <typename Route>
inline int16_t RouteSegmentId(const Route&, size_t, long)
{
    return 0;
}

// Curvature is heading change per unit of PATH length, not just angle between
// two arbitrary waypoints. This keeps a densely sampled bend from looking flat.
// C2: raised bands vs the original 40/55/85/130/190/260.
inline int16_t CornerLimitKmh(int32_t turnDeg, int32_t spanUnits)
{
    if (turnDeg <= 0) return kStraightMaxKmh;
    const int32_t curvature = (turnDeg * 1000) / Max(60, spanUnits);
    return static_cast<int16_t>(
        curvature > 80 ? 52 : curvature > 50 ? 70 :
        curvature > 25 ? 100 : curvature > 12 ? 150 :
        curvature > 5 ? 210 : 280);
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
// outBindingCornerLimitKmh (optional): tightest CornerLimit seen in the scan.
template <typename Route>
inline int16_t RouteSpeed(const Route& route, size_t current,
                          int32_t carXRaw, int32_t carZRaw,
                          int16_t* outBindingCornerLimitKmh = nullptr)
{
    const size_t count = route.centers.size();
    if (outBindingCornerLimitKmh) *outBindingCornerLimitKmh = kStraightMaxKmh;
    if (count < 3u || route.yawDeg.size() != count) return 82;
    current %= count;
    size_t next = (current + 1u) % count;
    int32_t dx = (route.centers[next].X.RawValue() - carXRaw) >> 16;
    int32_t dz = (route.centers[next].Z.RawValue() - carZRaw) >> 16;
    int32_t distance = Max(Abs(dx), Abs(dz));
    int32_t envelope = kStraightMaxKmh * kStraightMaxKmh;
    int16_t bindingLimit = kStraightMaxKmh;
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
        const int16_t section = SectionTargetKmh(RouteSegmentId(route, next, 0));
        const int16_t curvature = CornerLimitKmh(turn, span);
        const int16_t corner = section > 0 ? section : curvature;
        if (corner < bindingLimit) bindingLimit = corner;
        envelope = Min(envelope, corner * corner +
                       kBrakeKmhSquaredPerWorldUnit * distance);
        distance += span;
        previous = next;
        next = after;
    }
    if (outBindingCornerLimitKmh) *outBindingCornerLimitKmh = bindingLimit;
    // Dense routes can exhaust the fixed scan before the full braking horizon.
    // Do not authorize top speed beyond the part of PATH that was inspected.
    if (distance < 5000)
    {
        const int32_t visibleCap =
            BrakeApproachSpeed(kVisibleScanCapKmh, distance);
        envelope = Min(envelope, visibleCap * visibleCap);
    }
    return SpeedFromEnvelope(envelope);
}

// C1: pull the mid-rail lookahead point toward the inside of the upcoming bend.
template <typename Route>
inline void ApplyRacingLineOffset(const Route& route,
                                  size_t segFrom, size_t segTo,
                                  int32_t& ioXRaw, int32_t& ioZRaw)
{
    const size_t count = route.centers.size();
    if (count < 2u || route.yawDeg.size() != count) return;
    segFrom %= count;
    segTo %= count;
    if (segFrom == segTo) return;

    int32_t signedTurn =
        ShortestTurnSigned(route.yawDeg[segFrom], route.yawDeg[segTo]);
    const size_t after = (segTo + 1u) % count;
    if (after != segFrom)
    {
        const int32_t nextTurn =
            ShortestTurnSigned(route.yawDeg[segTo], route.yawDeg[after]);
        if (Abs(nextTurn) > Abs(signedTurn)) signedTurn = nextTurn;
    }
    if (Abs(signedTurn) < kRacingLineMinTurnDeg) return;

    const int32_t latUnits = Clamp(
        (-signedTurn * kRacingLineLatPerDeg) * kRacingLineSign,
        -kRacingLineMaxLatUnits,
        kRacingLineMaxLatUnits);
    if (latUnits == 0) return;

    // PATH segment forward in integer world units → Chebyshev-normalized Q16.
    const int32_t fxUnits =
        (route.centers[segTo].X.RawValue() - route.centers[segFrom].X.RawValue()) >> 16;
    const int32_t fzUnits =
        (route.centers[segTo].Z.RawValue() - route.centers[segFrom].Z.RawValue()) >> 16;
    const int32_t len = Max(1, Max(Abs(fxUnits), Abs(fzUnits)));
    const int32_t fxQ = (fxUnits << 16) / len;
    const int32_t fzQ = (fzUnits << 16) / len;
    const int32_t latRaw = latUnits << 16;
    // right = (-forwardZ, forwardX) — same basis as AutoLapTrafficPolicy::OffsetTarget.
    ioXRaw += static_cast<int32_t>(
        (-static_cast<int64_t>(fzQ) * latRaw) >> 16);
    ioZRaw += static_cast<int32_t>(
        (static_cast<int64_t>(fxQ) * latRaw) >> 16);
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
            // Lateral basis uses PATH segment endpoints, not the car→point chord.
            ApplyRacingLineOffset(route, from, to, outXRaw, outZRaw);
            return;
        }
        remaining -= span;
        from = to;
    }
    outXRaw = route.centers[from].X.RawValue();
    outZRaw = route.centers[from].Z.RawValue();
    ApplyRacingLineOffset(route, from, (from + 1u) % count, outXRaw, outZRaw);
}
} // namespace AutoLapSpeedPolicy
