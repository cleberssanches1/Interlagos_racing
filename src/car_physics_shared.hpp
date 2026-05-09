#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "interfaces.hpp"

namespace Game::CarPhysics
{
using Fxp = SRL::Math::Types::Fxp;

struct DynamicsState
{
    Fxp forwardSpeed = Fxp::BuildRaw(0);
    Fxp lateralSpeed = Fxp::BuildRaw(0);
    Fxp yawRateDegPerFrame = Fxp::BuildRaw(0);
    Fxp steerDeg = Fxp::BuildRaw(0);
};

struct GroundState
{
    Fxp surfaceYTarget = Fxp::BuildRaw(0);
    int16_t lastSurfaceSegmentId = -1;
    uint8_t surfaceProbeCooldown = 0u;
    uint8_t auxProbeCooldown = 0u;
    bool surfaceYInitialized = false;
};

struct FrameStepOutput
{
    Fxp speedAbs = Fxp::BuildRaw(0);
    Fxp sinYaw = Fxp::BuildRaw(0);
    Fxp cosYaw = Fxp::BuildRaw(0);
};

struct Tunables
{
    static constexpr int16_t kTargetTopSpeedKmh = 260;
    static constexpr std::array<uint16_t, 16> kDriveableFamilies = {
        337u, // F05564
        32u,  // F04764
        148u, // F01064
        2u,   // F01864
        74u,  // F02564
        321u, // F04364
        46u,  // F04664
        78u,  // F05464
        218u, // F00164
        77u,  // F00264
        169u, // F00364
        362u, // F00464
        367u, // F00564
        112u, // F06164
        283u, // F06264
        1u    // F06364
    };

    static constexpr uint8_t kSurfaceProbeIntervalFrames = 2u;
    static constexpr Fxp kEngineAccelPerFrame = Fxp::BuildRaw(0x00003852); // ~0.220
    static constexpr Fxp kBrakeDecelPerFrame = Fxp::BuildRaw(0x00003852);  // ~0.220
    static constexpr Fxp kAeroDragCoeff = Fxp::BuildRaw(0x00000083);       // ~0.0020
    static constexpr Fxp kRollingDragCoeff = Fxp::BuildRaw(0x000001AA);    // ~0.0065
    static constexpr Fxp kCoastDampingPerFrame = Fxp::BuildRaw(0x00000106);// ~0.0040
    static constexpr Fxp kMaxForwardSpeed = Fxp::BuildRaw(0x00070000);     // ~7.0
    static constexpr Fxp kMaxSteerDeg = Fxp::BuildRaw(6 << 16);            // 6 deg
    static constexpr Fxp kSteerResponse = Fxp::BuildRaw(0x00006000);       // ~0.375
    static constexpr Fxp kHighSpeedSteerLoss = Fxp::BuildRaw(0x00008000);  // 0.5
    static constexpr Fxp kYawFromSpeedCoeff = Fxp::BuildRaw(0x00003000);   // ~0.1875
    static constexpr Fxp kYawDamping = Fxp::BuildRaw(0x00002000);          // 0.125
    static constexpr Fxp kLateralCouplingCoeff = Fxp::BuildRaw(0x00000800);// 0.03125
    static constexpr Fxp kLateralDampingCoeff = Fxp::BuildRaw(0x00004000); // 0.25
    static constexpr Fxp kRideHeightOffset = Fxp::BuildRaw(-(1 << 13));    // -0.125
    static constexpr Fxp kFastProbeSpeedThreshold = Fxp::BuildRaw(0x00050000); // 5.0
    static constexpr Fxp kMaxYStepUpPerFrame = Fxp::BuildRaw(0x00004000);      // 0.25
    static constexpr Fxp kMaxYStepDownPerFrame = Fxp::BuildRaw(0x00010000);    // 1.0
    static constexpr Fxp kSnapDownThreshold = Fxp::BuildRaw(0x0000C000);       // 0.75
    static constexpr Fxp kSurfaceSampleDownBias = Fxp::BuildRaw(0x00020000);   // 2.0
    static constexpr Fxp kProbeFrontBase = Fxp::BuildRaw(0x00008000);          // 0.5
    static constexpr Fxp kProbeFrontSpeedScale = Fxp::BuildRaw(0x00004000);    // 0.25
    static constexpr Fxp kProbeFrontMin = Fxp::BuildRaw(0x00008000);           // 0.5
    static constexpr Fxp kProbeFrontMax = Fxp::BuildRaw(0x00028000);           // 2.5
    static constexpr Fxp kProbeSlopeAssistSpeed = Fxp::BuildRaw(0x00010000);   // 1.0
    static constexpr uint8_t kAuxProbeCadenceFrames = 3u;
};

inline Fxp Clamp(const Fxp& value, const Fxp& minValue, const Fxp& maxValue)
{
    return Fxp::Max(minValue, Fxp::Min(value, maxValue));
}

inline Fxp NormalizePercent(int16_t value)
{
    const int16_t clamped = std::clamp<int16_t>(value, static_cast<int16_t>(-100), static_cast<int16_t>(100));
    return Fxp::BuildRaw((static_cast<int32_t>(clamped) << 16) / 100);
}

inline int32_t NormalizeYaw(int32_t yawDeg)
{
    int32_t y = yawDeg % 360;
    if (y < 0) y += 360;
    return y;
}

inline int16_t BuildSpeedProxy(const Fxp& forwardSpeed)
{
    const Fxp normalized =
        Clamp(forwardSpeed / Tunables::kMaxForwardSpeed,
              Fxp::BuildRaw(0),
              Fxp::BuildRaw(1 << 16));
    const Fxp topKmh = Fxp::BuildRaw(Tunables::kTargetTopSpeedKmh << 16);
    return (normalized * topKmh).As<int16_t>();
}

inline int16_t FxpToDebugInt(const Fxp& value)
{
    const int32_t asInt = value.RawValue() >> 16;
    return static_cast<int16_t>(std::clamp<int32_t>(asInt, -32768, 32767));
}

inline uint8_t SegmentAffinityScore(int32_t segmentId, int32_t seedSegmentId)
{
    if (segmentId <= 0 || seedSegmentId <= 0) return 0u;
    if (segmentId == seedSegmentId) return 2u;
    int32_t delta = segmentId - seedSegmentId;
    if (delta < 0) delta = -delta;
    return (delta <= 2) ? 1u : 0u;
}

inline void ResetGroundDebug(GameplayFrameState& ioFrameState)
{
    ioFrameState.debugGroundMask = 0u;
    ioFrameState.debugGroundYRear = 0;
    ioFrameState.debugGroundYFront = 0;
    ioFrameState.debugGroundYTarget = 0;
}
} // namespace Game::CarPhysics

