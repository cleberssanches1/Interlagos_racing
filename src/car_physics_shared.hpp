#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "interfaces.hpp"
#include "physics_feature_flags.hpp"

namespace Game::CarPhysics
{
using Fxp = SRL::Math::Types::Fxp;

struct DynamicsState
{
    Fxp forwardSpeed = Fxp::BuildRaw(0);
    Fxp lateralSpeed = Fxp::BuildRaw(0);
    Fxp yawRateDegPerFrame = Fxp::BuildRaw(0);
    Fxp steerDeg = Fxp::BuildRaw(0);
    Fxp surfaceGripScale = Fxp::BuildRaw(1 << 16);
    int32_t yawAccumulatorDegRaw = 0; // 16.16 integrated yaw delta
    uint8_t gear = 1u;                // 1..6 forward gears
    int16_t engineRpm = 1000;         // debug/telemetry
    uint8_t launchStraightFrames = 0u;
    bool steerLaunchArmed = true;
};

struct GroundState
{
    Fxp surfaceYTarget = Fxp::BuildRaw(0);
    Fxp surfaceYFiltered = Fxp::BuildRaw(0);
    Fxp correctionX = Fxp::BuildRaw(0);
    Fxp correctionZ = Fxp::BuildRaw(0);
    Fxp lastStableX = Fxp::BuildRaw(0);
    Fxp lastStableZ = Fxp::BuildRaw(0);
    int16_t lastSurfaceSegmentId = -1;
    int16_t lastSurfaceFaceIndex = -1;
    uint16_t lastSurfaceFamilyId = 0u;
    uint8_t lastSurfaceType = 0u;
    uint8_t surfaceProbeCooldown = 0u;
    uint8_t auxProbeCooldown = 0u;
    bool hasGroundSupport = false;
    bool edgeLeftLost = false;
    bool edgeRightLost = false;
    bool lastStablePlanarInitialized = false;
    bool surfaceYInitialized = false;
    bool surfaceYFilterInitialized = false;
    int32_t lastWallQueryFrameId = -1;
    int32_t lastWallApplyFrameId = -1;
    bool lastWallQueryHit = false;
    int32_t lastWallQuerySegmentId = -1;
    Fxp lastWallPushX = Fxp::BuildRaw(0);
    Fxp lastWallPushZ = Fxp::BuildRaw(0);
};

struct FrameStepOutput
{
    Fxp speedAbs = Fxp::BuildRaw(0);
    Fxp sinYaw = Fxp::BuildRaw(0);
    Fxp cosYaw = Fxp::BuildRaw(0);
    Fxp planarDx = Fxp::BuildRaw(0);
    Fxp planarDz = Fxp::BuildRaw(0);
    int16_t yawStepDeg = 0;
};

struct Tunables
{
    static constexpr bool kEnableSurfaceTypeQuery =
        Game::PhysicsFeatureFlags::kEnableSurfaceTypeQuery;
    static constexpr bool kEnableFaceCache =
        Game::PhysicsFeatureFlags::kEnableFaceCache;
    static constexpr uint8_t kSurfaceTypeAsphalt = 1u;
    static constexpr uint8_t kSurfaceTypeEscapeArea = 2u;
    static constexpr uint8_t kSurfaceTypeGrass = 3u;
    static constexpr int16_t kTargetTopSpeedKmh = 300;
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
    static constexpr uint8_t kLowDynamicsProbeIntervalFrames = 2u;
    static constexpr uint8_t kMediumDynamicsProbeIntervalFrames = 2u;
    static constexpr int16_t kLowDynamicsSpeedProxyThreshold = 30; // km/h
    static constexpr int16_t kLowDynamicsSteeringThreshold = 8;     // percent
    static constexpr int16_t kMediumDynamicsSpeedProxyMin = 25;     // km/h
    static constexpr int16_t kMediumDynamicsSpeedProxyMax = 180;    // km/h
    static constexpr int16_t kMediumDynamicsSteeringThreshold = 6;  // percent
    static constexpr uint16_t kAsphaltFamilyId = 1u; // base asphalt family
    static constexpr std::array<uint8_t, 1> kAsphaltSurfaceTypes = {
        kSurfaceTypeAsphalt
    };
    static constexpr std::array<uint8_t, 3> kDriveableSurfaceTypes = {
        kSurfaceTypeAsphalt,
        kSurfaceTypeEscapeArea,
        kSurfaceTypeGrass
    };
    static constexpr Fxp kGripScaleAsphalt = Fxp::BuildRaw(1 << 16);
    static constexpr Fxp kGripScaleOffroad = Fxp::BuildRaw(0x0000B333); // ~0.70
    static constexpr Fxp kGripScaleFallback = Fxp::BuildRaw(0x0000999A); // ~0.60
    static constexpr Fxp kEngineAccelPerFrame = Fxp::BuildRaw(0x000047AE); // base (legacy)
    static constexpr std::array<Fxp, 6> kGearAccelPerFrame = {
        Fxp::BuildRaw(0x00007AE1), // 0.48
        Fxp::BuildRaw(0x00006666), // 0.40
        Fxp::BuildRaw(0x000051EC), // 0.32
        Fxp::BuildRaw(0x00004189), // 0.256
        Fxp::BuildRaw(0x00003333), // 0.20
        Fxp::BuildRaw(0x000028F6)  // 0.16
    };
    static constexpr std::array<int16_t, 6> kGearTopSpeedKmh = {
        95, 130, 170, 215, 255, 300
    };
    static constexpr int16_t kEngineIdleRpm = 2200;
    static constexpr int16_t kEngineUpShiftRpm = 12800;
    static constexpr int16_t kEngineDownShiftRpm = 7800;
    static constexpr int16_t kEngineMaxRpm = 13500;
    static constexpr Fxp kReverseAccelPerFrame = Fxp::BuildRaw(0x00002000); // 0.125
    // Forward launch while steering from standstill:
    // keep initial traction similar to reverse to reduce side kick.
    static constexpr Fxp kForwardSteerLaunchAccelPerFrame = Fxp::BuildRaw(0x00003000); // 0.1875
    static constexpr Fxp kForwardSteerLaunchSpeedThreshold = Fxp::BuildRaw(0x00004000); // 0.25 (~10.5 km/h)
    static constexpr Fxp kBrakeDecelPerFrame = Fxp::BuildRaw(0x0000570A);  // ~0.340
    static constexpr Fxp kBrakeStopSpeedThreshold = Fxp::BuildRaw(0x0000A000); // ~0.625
    static constexpr uint8_t kReverseEngageDelayFrames = 10u; // brake deadzone before reverse
    static constexpr Fxp kAeroDragCoeff = Fxp::BuildRaw(0x00000058);       // ~0.00134
    static constexpr Fxp kRollingDragCoeff = Fxp::BuildRaw(0x000000D7);    // ~0.0033
    static constexpr Fxp kCoastDampingPerFrame = Fxp::BuildRaw(0x000000A4);// ~0.0025
    static constexpr Fxp kCoastStopSpeedThreshold = Fxp::BuildRaw(0x00010000); // 1.0
    static constexpr Fxp kMaxForwardSpeed = Fxp::BuildRaw(0x00072000);     // ~7.125
    static constexpr Fxp kMaxReverseSpeed = Fxp::BuildRaw(0x00028000);     // ~2.5
    static constexpr Fxp kMaxSteerDeg = Fxp::BuildRaw(12 << 16);           // 12 deg
    static constexpr Fxp kSteerResponse = Fxp::BuildRaw(0x00003333);       // 0.20
    static constexpr Fxp kHighSpeedSteerLoss = Fxp::BuildRaw(0x0000B333);  // ~0.70
    static constexpr Fxp kSteerAuthorityMin = Fxp::BuildRaw(0x00003333);   // 0.20
    static constexpr Fxp kYawWheelbaseUnits = Fxp::BuildRaw(18 << 16);     // kept for compatibility
    static constexpr Fxp kWheelbaseFront = Fxp::BuildRaw(0x0000F333);      // ~0.95
    static constexpr Fxp kWheelbaseRear = Fxp::BuildRaw(0x0000F333);       // ~0.95
    static constexpr Fxp kSlipDenomMin = Fxp::BuildRaw(0x0000599A);        // ~0.35
    static constexpr Fxp kLateralStiffnessFront = Fxp::BuildRaw(0x00026666); // ~2.40
    static constexpr Fxp kLateralStiffnessRear = Fxp::BuildRaw(0x0002CCCC);  // ~2.80
    static constexpr Fxp kLateralForceCapBase = Fxp::BuildRaw(0x00028000); // 2.50
    static constexpr Fxp kYawMomentGain = Fxp::BuildRaw(0x00007000);       // ~0.4375
    static constexpr Fxp kYawCoupling = Fxp::BuildRaw(0x000028F6);         // ~0.16
    static constexpr Fxp kYawRateResponse = Fxp::BuildRaw(0x00008000);     // 0.50
    static constexpr Fxp kMaxYawRateDegPerFrame = Fxp::BuildRaw(0x00024000); // 2.25 deg/frame
    static constexpr Fxp kDegToRad = Fxp::BuildRaw(0x00000478);            // 0.01745
    static constexpr Fxp kRadToDeg = Fxp::BuildRaw(0x00394BC7);            // 57.2958
    static constexpr Fxp kYawFromSpeedCoeff = Fxp::BuildRaw(0x00000600);   // ~0.0234
    static constexpr Fxp kYawDamping = Fxp::BuildRaw(0x00003000);          // 0.1875
    static constexpr Fxp kLateralCouplingCoeff = Fxp::BuildRaw(0x00000040);// ~0.0010
    static constexpr Fxp kLateralDampingCoeff = Fxp::BuildRaw(0x00008000); // 0.50
    static constexpr Fxp kCoastLateralDampingCoeff = Fxp::BuildRaw(0x0000B000); // 0.6875
    static constexpr Fxp kCoastYawDampingCoeff = Fxp::BuildRaw(0x0000A000);     // 0.625
    static constexpr Fxp kCoastResidualForwardCutoff = Fxp::BuildRaw(0x00004000); // 0.25
    static constexpr Fxp kCoastResidualLateralCutoff = Fxp::BuildRaw(0x00004000); // 0.25
    static constexpr Fxp kCoastResidualYawCutoff = Fxp::BuildRaw(0x00004000);     // 0.25 deg/frame
    static constexpr int16_t kCoastSteerCenterThreshold = 10;                      // percent
    static constexpr Fxp kCoastNoSlideSpeedThreshold = Fxp::BuildRaw(0x00018000);  // 1.5
    static constexpr Fxp kCoastNoSlideSteerScale = Fxp::BuildRaw(0x00002000);      // 0.125
    static constexpr Fxp kCoastNoSlideLateralDamping = Fxp::BuildRaw(0x0000E000);   // 0.875
    static constexpr Fxp kCoastNoSlideYawDamping = Fxp::BuildRaw(0x0000D000);       // 0.8125
    static constexpr Fxp kCoastNoSlideLateralCutoff = Fxp::BuildRaw(0x00002000);    // 0.125
    static constexpr Fxp kCoastNoSlideYawCutoff = Fxp::BuildRaw(0x00002000);        // 0.125 deg/frame
    static constexpr Fxp kBrakeLateralDampingCoeff = Fxp::BuildRaw(0x0000D000); // ~0.8125
    static constexpr Fxp kBrakeYawDampingCoeff = Fxp::BuildRaw(0x0000C000);     // 0.75
    static constexpr Fxp kBrakeResidualLateralCutoff = Fxp::BuildRaw(0x00004000); // 0.25
    static constexpr Fxp kBrakeResidualYawCutoff = Fxp::BuildRaw(0x00004000);     // 0.25 deg/frame
    // Low-speed launch handling:
    // use a kinematic yaw model and suppress lateral slide until speed stabilizes.
    static constexpr Fxp kLaunchKinematicSpeedThreshold = Fxp::BuildRaw(0x00028000); // 2.5
    static constexpr Fxp kLaunchPureForwardSpeedThreshold = Fxp::BuildRaw(0x00002000); // 0.125 (~5.3 km/h)
    static constexpr Fxp kLaunchCrawlSpeedThreshold = Fxp::BuildRaw(0x00002000); // 0.125 (~5.3 km/h)
    static constexpr Fxp kLaunchYawRateResponse = Fxp::BuildRaw(0x0000A000);         // 0.625
    // Zero-speed launch with steering:
    // immediate turn entry (no forced straight frame).
    static constexpr uint8_t kLaunchStraightFrameCount = 0u;
    static constexpr Fxp kLaunchStraightEntrySpeed = Fxp::BuildRaw(0x00014000); // 1.25
    static constexpr Fxp kRideHeightOffset = Fxp::BuildRaw(-(1 << 14));    // -0.25
    static constexpr Fxp kFastProbeSpeedThreshold = Fxp::BuildRaw(0x00050000); // 5.0
    static constexpr Fxp kMaxYStepUpPerFrame = Fxp::BuildRaw(0x00010000);      // 1.0
    static constexpr Fxp kMaxYStepDownPerFrame = Fxp::BuildRaw(0x00014000);    // 1.25
    static constexpr Fxp kSnapDownThreshold = Fxp::BuildRaw(0x00008000);       // 0.5
    static constexpr Fxp kSnapUpThreshold = Fxp::BuildRaw(0x00008000);         // 0.5
    static constexpr Fxp kSurfaceSampleDownBias = Fxp::BuildRaw(0x00020000);   // 2.0
    static constexpr Fxp kProbeFrontBase = Fxp::BuildRaw(0x00014000);          // 1.25
    static constexpr Fxp kProbeFrontSpeedScale = Fxp::BuildRaw(0x00003000);    // 0.1875
    static constexpr Fxp kProbeFrontMin = Fxp::BuildRaw(0x00010000);           // 1.0
    static constexpr Fxp kProbeFrontMax = Fxp::BuildRaw(0x00030000);           // 3.0
    static constexpr Fxp kProbeSlopeAssistSpeed = Fxp::BuildRaw(0x00010000);   // 1.0
    static constexpr uint8_t kAuxProbeCadenceFrames = 2u;
    static constexpr uint8_t kGripProbeIntervalFrames = 2u;
    static constexpr Fxp kProbeHalfWheelBase = Fxp::BuildRaw(0x0000D999);      // ~0.85
    static constexpr Fxp kProbeHalfTrack = Fxp::BuildRaw(0x00008CCC);          // ~0.55
    static constexpr Fxp kWallCollisionRadius = Fxp::BuildRaw(0x00012666);     // ~1.15
    static constexpr bool kEnableWallPlanarPush =
        Game::PhysicsFeatureFlags::kEnableWallCollisionRuntime;
    static constexpr Fxp kEdgeRecoverPush = Fxp::BuildRaw(0);                  // disabled (bias fix)
    static constexpr Fxp kMaxPlanarCorrectionPerFrame = Fxp::BuildRaw(0x00008000); // 0.50
    static constexpr Fxp kNoSupportSpeedDamping = Fxp::BuildRaw(0x00010000);   // 1.0
    static constexpr Fxp kEdgeForwardDamping = Fxp::BuildRaw(0x0000A000);      // 0.625
    static constexpr Fxp kEdgeLateralDamping = Fxp::BuildRaw(0x0000E000);      // 0.875
    // Vertical smoothing for chassis over sharp face joins.
    // 0.30 means 30 percent of target delta per frame (about 70 percent smoothing).
    static constexpr Fxp kChassisVerticalFollowAlpha = Fxp::BuildRaw(0x00002666); // ~0.15 (50 percent smoother)
    // Allow hard snap only when error is very large to avoid micro hops.
    static constexpr Fxp kChassisVerticalHardSnapThreshold = Fxp::BuildRaw(0x00030000); // 3.0
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
    const Fxp speedAbs = forwardSpeed.Abs();
    const Fxp normalized =
        Clamp(speedAbs / Tunables::kMaxForwardSpeed,
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
    ioFrameState.debugGroundYRearRaw = 0;
    ioFrameState.debugGroundYFrontRaw = 0;
    ioFrameState.debugSteerDeg = 0;
    ioFrameState.debugYawRateDeg = 0;
    ioFrameState.debugYawStepDeg = 0;
    ioFrameState.debugEngineRpm = 0;
    ioFrameState.debugGear = 1;
    ioFrameState.debugSpeedKmh = 0;
    ioFrameState.debugPlanarDx = 0;
    ioFrameState.debugPlanarDz = 0;
    ioFrameState.debugNetDx = 0;
    ioFrameState.debugNetDz = 0;
    ioFrameState.debugCorrX = 0;
    ioFrameState.debugCorrZ = 0;
    ioFrameState.debugWallHit = 0u;
    ioFrameState.debugWallPushX = 0;
    ioFrameState.debugWallPushZ = 0;
    ioFrameState.debugWallSegmentId = -1;
    ioFrameState.groundFaceIndex = -1;
    ioFrameState.groundFamilyId = 0u;
    ioFrameState.groundSurfaceType = 0u;
}

inline Fxp& RuntimeRideHeightOffset()
{
    static Fxp value = Tunables::kRideHeightOffset;
    return value;
}

inline Fxp GetRideHeightOffset()
{
    return RuntimeRideHeightOffset();
}

inline void SetRideHeightOffset(const Fxp& value)
{
    RuntimeRideHeightOffset() = value;
}
} // namespace Game::CarPhysics
