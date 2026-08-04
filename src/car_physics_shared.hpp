#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "interfaces.hpp"
#include "car_arcade_suspension.hpp"
#include "car_contact_geometry.hpp"
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
    int16_t engineRpm = 4200;         // debug/telemetry
    int16_t engineRpmVisual = 4200;   // audio/telemetry with shift transient
    int16_t shiftHoldRpm = 4200;
    int8_t gear = 0;                  // -1=reverse, 0=neutral, 1..kForwardGearCount forward gears
    uint8_t shiftTransientFrames = 0u;
    uint8_t shiftHoldFrames = 0u;
    uint8_t launchStraightFrames = 0u;
    uint8_t forwardLaunchLateralLockFrames = 0u;
    uint8_t brakeDriftFrames = 0u;
    bool neutralHeldManually = false;
    bool steerLaunchArmed = true;
    bool wasKinematic = false;
    bool wasBraking = false;
};

struct GroundState
{
    ArcadeSuspensionState suspension{};
    Fxp surfaceYTarget = Fxp::BuildRaw(0);
    Fxp surfaceYFiltered = Fxp::BuildRaw(0);
    Fxp verticalVelocity = Fxp::BuildRaw(0);
    Fxp correctionX = Fxp::BuildRaw(0);
    Fxp correctionZ = Fxp::BuildRaw(0);
    Fxp lastStableX = Fxp::BuildRaw(0);
    Fxp lastStableZ = Fxp::BuildRaw(0);
    int32_t lastWallQueryFrameId = -1;
    int32_t lastWallApplyFrameId = -1;
    int16_t lastWallQuerySegmentId = -1;
    int16_t lastSurfaceSegmentId = -1;
    int16_t lastSurfaceFaceIndex = -1;
    uint16_t lastSurfaceFamilyId = 0u;
    uint8_t lastSurfaceType = 0u;
    uint8_t surfaceProbeCooldown = 0u;
    uint8_t auxProbeCooldown = 0u;
    uint8_t surfaceContactCooldown = 0u;
    bool hasGroundSupport = false;
    bool edgeLeftLost = false;
    bool edgeRightLost = false;
    bool lastStablePlanarInitialized = false;
    bool surfaceYInitialized = false;
    // Frames to keep last Y after a probe miss (anti-stuck / anti-pop).
    uint8_t surfaceContactFrames = 0u;
    bool surfaceYFilterInitialized = false;
    bool lastWallQueryHit = false;
    Fxp lastWallPushX = Fxp::BuildRaw(0);
    Fxp lastWallPushZ = Fxp::BuildRaw(0);
    // Previous-frame |front-rear| grade (world Y) — debug / telemetry.
    int16_t lastSlopeAbsY = 0;
    // Signed road grade tanθ ≈ (frontY − rearY) / wheelbase (16.16).
    // Y-down: tan > 0 ⇒ nose lower ⇒ downhill when moving forward.
    int32_t gradeTanRaw = 0;
    bool gradeValid = false;
    // Natural descent: last accepted surface target + drop window.
    int32_t lastAcceptedSurfaceYRaw = 0;
    bool lastAcceptedSurfaceYValid = false;
    // Coherent two-frame diagonal cycle: committed plane plus its per-frame
    // velocity, used to reconstruct a 60 Hz equilibrium target.
    int32_t committedSurfaceYRaw = 0;
    int32_t surfaceTargetVelocityRaw = 0;
    bool committedSurfaceYValid = false;
    uint8_t topologyDropFrames = 0u;
};

struct SurfaceQueryResult
{
    bool valid = false;
    bool hasDriveableSupport = false;
    int32_t segmentId = -1;
    int16_t faceIndex = -1;
    uint16_t familyId = 0u;
    uint8_t surfaceType = 0u;
    Fxp surfaceY = Fxp::BuildRaw(0);
    Vector3D normal = Vector3D(0.0, -1.0, 0.0);
};

struct BodyClip
{
    Fxp localForward = Fxp::BuildRaw(0);
    Fxp localRight = Fxp::BuildRaw(0);
    Fxp force = Fxp::BuildRaw(0);
    Fxp dampening = Fxp::BuildRaw(0);
};

struct FrameStepOutput
{
    Fxp speedAbs = Fxp::BuildRaw(0);
    Fxp sinYaw = Fxp::BuildRaw(0);
    Fxp cosYaw = Fxp::BuildRaw(0);
    Fxp planarDx = Fxp::BuildRaw(0);
    Fxp planarDz = Fxp::BuildRaw(0);
    int16_t yawStepDeg = 0;
    bool wasKinematicMode = false;
};

struct Tunables
{
    static constexpr bool kEnableSaturnLowCostPhysics =
        Game::PhysicsFeatureFlags::kEnableSaturnLowCostPhysics;
    static constexpr bool kEnableSurfaceTypeQuery =
        Game::PhysicsFeatureFlags::kEnableSurfaceTypeQuery;
    static constexpr bool kEnableFaceCache =
        Game::PhysicsFeatureFlags::kEnableFaceCache;
    static constexpr bool kEnableWheelStrictSurface =
        Game::PhysicsFeatureFlags::kEnableWheelStrictSurface;
    static constexpr uint8_t kSurfaceTypeAsphalt = 1u;
    static constexpr uint8_t kSurfaceTypeEscapeArea = 2u;
    static constexpr uint8_t kSurfaceTypeGrass = 3u;
    // Gearbox/car tune:
    // the dynamics code reads only these tables and helpers, which keeps room
    // for future per-car profiles and a manual shift mode without rewriting
    // the integration path.
    static constexpr int8_t kReverseGear = -1;
    static constexpr int8_t kNeutralGear = 0;
    static constexpr uint8_t kForwardGearCount = 8u;
    static constexpr bool kAutomaticGearboxEnabled = true;
    static constexpr int16_t kTargetTopSpeedKmh = 335;
    // PATH.NYA closed-loop XZ length: 84689.019 world units.
    // Real Interlagos lap length: 4309 m.
    // Derived scale: 19.653985 world units / meter.
    // At 30 FPS, 1 world unit / frame = 5.495069 km/h.
    static constexpr Fxp kWorldUnitsPerMeter = Fxp::BuildRaw(0x0013A76C);       // ~19.653985
    static constexpr Fxp kKmhPerWorldUnitPerFrame = Fxp::BuildRaw(0x00057EBD); // ~5.495069
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
    // Cost cut 1e: reuse surface probe longer in low dynamics. Rollback: 6u -> 4u.
    static constexpr uint8_t kLowDynamicsProbeIntervalFrames =
        kEnableSaturnLowCostPhysics ? 6u : 2u;
    // Cost cut 1f: medium-dynamics surface reuse. Rollback: 6u -> 4u.
    static constexpr uint8_t kMediumDynamicsProbeIntervalFrames =
        kEnableSaturnLowCostPhysics ? 6u : 2u;
    // Cost cut 1h: low-dyn reuse up to 40 km/h. Rollback: 40 -> 30.
    static constexpr int16_t kLowDynamicsSpeedProxyThreshold = 40; // km/h
    // Cost cut 1i: low-dyn reuse with slightly more steer. Rollback: 12 -> 8.
    static constexpr int16_t kLowDynamicsSteeringThreshold = 12;    // percent
    static constexpr int16_t kMediumDynamicsSpeedProxyMin = 25;     // km/h
    static constexpr int16_t kMediumDynamicsSpeedProxyMax = 180;    // km/h
    // Cost cut 1g: more frames count as medium-dyn (reuse path). Rollback: 12 -> 6.
    static constexpr int16_t kMediumDynamicsSteeringThreshold = 12; // percent
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
    // Target full-throttle band times for the Interlagos/F1 profile:
    // G1 0-95 in 2.3s, G2 95-140 in 1.1s, G3 140-185 in 1.0s,
    // G4 185-225 in 0.9s, G5 225-265 in 1.0s, G6 265-295 in 1.0s,
    // G7 295-320 in 1.2s, G8 320-335 in 1.5s.
    static constexpr std::array<uint8_t, kForwardGearCount> kGearBandTargetFrames = {
        69u, 33u, 30u, 27u, 30u, 30u, 36u, 45u
    };
    // Target net longitudinal acceleration per frame at full throttle.
    // These values are derived from the requested speed bands and times,
    // and represent the post-drag speed rise we want the car to achieve
    // while each gear is active.
    static constexpr std::array<Fxp, kForwardGearCount> kGearAccelPerFrame = {
        Fxp::BuildRaw(0x00004024), // ~0.2506
        Fxp::BuildRaw(0x00003F87), // ~0.2482
        Fxp::BuildRaw(0x000045E1), // ~0.2730
        Fxp::BuildRaw(0x00004505), // ~0.2696
        Fxp::BuildRaw(0x00003E1E), // ~0.2426
        Fxp::BuildRaw(0x00002E96), // ~0.1820
        Fxp::BuildRaw(0x0000205A), // ~0.1264
        Fxp::BuildRaw(0x00000F87)  // ~0.0607
    };
    static constexpr std::array<int16_t, kForwardGearCount> kGearTopSpeedKmh = {
        95, 140, 185, 225, 265, 295, 320, 335
    };
    static constexpr int16_t kEngineIdleRpm = 4200;
    static constexpr int16_t kStationaryShiftMaxKmh = 3;
    static constexpr int16_t kReverseTopSpeedKmh = 100;
    static constexpr int16_t kReverseShiftMaxKmh = 25;
    static constexpr int16_t kEngineUpShiftRpm = 12500;
    static constexpr int16_t kEngineDownShiftRpm = 9600;
    static constexpr int16_t kAutoDownshiftSpeedHysteresisKmh = 3;
    static constexpr int16_t kEngineMaxRpm = 12500;
    static constexpr int16_t kEngineHardMaxRpm = 13500;
    static constexpr int16_t kNeutralFreeRevMaxRpm = 12500;
    static constexpr int16_t kReverseLoadedMinRpm = 5200;
    static constexpr int16_t kReverseLoadedMaxRpm = 10800;
    static constexpr int16_t kShiftRpmDropUp = 2100;
    static constexpr int16_t kShiftRpmDropDown = -900;
    static constexpr int16_t kShiftRpmRecoverPerFrame = 180;
    static constexpr uint8_t kShiftTransientFrames = 10u;
    static constexpr uint8_t kShiftRpmHoldFrames = 2u;
    static constexpr int16_t kDownshiftTargetWindowBelowRpm = 150;
    static constexpr int16_t kDownshiftTargetWindowAboveCoastRpm = 100;
    static constexpr int16_t kDownshiftTargetWindowAboveBrakeRpm = 220;
    static constexpr int16_t kDownshiftTargetWindowAboveThrottleRpm = 320;
    static constexpr uint16_t kNeutralRpmRisePerFrame = 240u;
    static constexpr uint16_t kNeutralRpmDropPerFrame = 300u;
    static constexpr std::array<int16_t, kForwardGearCount> kGearMinTypicalSpeedKmh = {
        0, 75, 110, 145, 185, 225, 265, 295
    };
    static constexpr std::array<int16_t, kForwardGearCount> kGearLoadedMinRpm = {
        9500, 9200, 9000, 9100, 9300, 9400, 9500, 10200
    };
    static constexpr std::array<int16_t, kForwardGearCount> kGearLoadedMaxRpm = {
        12500, 12500, 12500, 12500, 12500, 12500, 12500, 12500
    };
    static constexpr std::array<int16_t, kForwardGearCount> kGearShiftUpRpm = {
        12500, 12500, 12500, 12500, 12500, 12500, 12500, 12500
    };
    static constexpr std::array<int16_t, kForwardGearCount> kGearShiftLandingMinRpm = {
        9500, 9200, 9000, 9100, 9300, 9400, 9500, 10200
    };
    static constexpr std::array<int16_t, kForwardGearCount> kGearShiftLandingMaxRpm = {
        9500, 9200, 9000, 9100, 9300, 9400, 9500, 10200
    };
    static constexpr std::array<int16_t, kForwardGearCount> kGearShiftLandingDipRpm = {
        0, 3300, 3500, 3400, 3200, 3100, 3000, 2300
    };
    static constexpr std::array<uint16_t, kForwardGearCount> kGearRpmRisePerFrame = {
        180u, 150u, 120u, 100u, 85u, 72u, 62u, 54u
    };
    static constexpr std::array<uint16_t, kForwardGearCount> kGearRpmDropPerFrame = {
        230u, 210u, 190u, 170u, 150u, 135u, 120u, 110u
    };
    static constexpr int16_t kHighSpeedAeroStartKmh = 320;
    static constexpr int16_t kHighSpeedAeroFullKmh = 335;
    static constexpr std::array<Fxp, kForwardGearCount> kGearHighSpeedAeroExtraScale = {
        Fxp::BuildRaw(0x00000000), // 0.00
        Fxp::BuildRaw(0x00000000), // 0.00
        Fxp::BuildRaw(0x00000000), // 0.00
        Fxp::BuildRaw(0x00000000), // 0.00
        Fxp::BuildRaw(0x00000000), // 0.00
        Fxp::BuildRaw(0x00000000), // 0.00
        Fxp::BuildRaw(0x00000000), // 0.00
        Fxp::BuildRaw(0x00003852)  // 0.22
    };
    static constexpr std::array<Fxp, kForwardGearCount> kGearHighSpeedAccelMinScale = {
        Fxp::BuildRaw(0x00010000), // 1.00
        Fxp::BuildRaw(0x00010000), // 1.00
        Fxp::BuildRaw(0x00010000), // 1.00
        Fxp::BuildRaw(0x00010000), // 1.00
        Fxp::BuildRaw(0x00010000), // 1.00
        Fxp::BuildRaw(0x00010000), // 1.00
        Fxp::BuildRaw(0x00010000), // 1.00
        Fxp::BuildRaw(0x0000CCCC)  // 0.80
    };
    static constexpr Fxp kReverseAccelPerFrame = Fxp::BuildRaw(0x0000E8F0); // ~0.9099
    // Forward launch while steering from standstill:
    // keep initial traction similar to reverse to reduce side kick.
    static constexpr Fxp kForwardSteerLaunchAccelPerFrame = Fxp::BuildRaw(0x000023D7); // ~0.14
    static constexpr Fxp kForwardSteerLaunchSpeedThreshold = Fxp::BuildRaw(0x0001D1DF); // ~1.8198
    static constexpr Fxp kBrakeDecelPerFrame = Fxp::BuildRaw(0x0000999A);  // ~0.60
    static constexpr Fxp kBrakeStopSpeedThreshold = Fxp::BuildRaw(0x0000C000); // 0.75
    static constexpr Fxp kAeroDragCoeff = Fxp::BuildRaw(0x00000004);       // ~0.00006
    static constexpr Fxp kRollingDragCoeff = Fxp::BuildRaw(0x00000050);    // ~0.0012
    static constexpr Fxp kCoastDampingPerFrame = Fxp::BuildRaw(0x000004A9);// ~0.0182
    static constexpr Fxp kCoastStopSpeedThreshold = Fxp::BuildRaw(0x0007477D); // ~7.2793
    static constexpr Fxp kMaxForwardSpeed = Fxp::BuildRaw(0x003CF6B8);     // ~60.9637 => 335 km/h
    static constexpr Fxp kMaxReverseSpeed = Fxp::BuildRaw(0x001232B9);     // ~18.1981
    static constexpr Fxp kMaxSteerDeg = Fxp::BuildRaw(12 << 16);           // 12 deg
    static constexpr Fxp kInvMaxSteerDeg = Fxp::BuildRaw(0x00001555);      // ~1/12
    static constexpr Fxp kSteerResponse = Fxp::BuildRaw(0x00003333);       // 0.20
    static constexpr Fxp kHighSpeedSteerLoss = Fxp::BuildRaw(0x0000B333);  // ~0.70
    static constexpr Fxp kSteerAuthorityMin = Fxp::BuildRaw(0x00003333);   // 0.20
    static constexpr Fxp kYawWheelbaseUnits = Fxp::BuildRaw(18 << 16);     // kept for compatibility
    static constexpr Fxp kWheelbaseFront = Fxp::BuildRaw(0x0000F333);      // ~0.95
    static constexpr Fxp kWheelbaseRear = Fxp::BuildRaw(0x0000F333);       // ~0.95
    static constexpr Fxp kSlipDenomMin = Fxp::BuildRaw(0x00028C39);        // ~2.5477
    static constexpr Fxp kLateralStiffnessFront = Fxp::BuildRaw(0x00026666); // ~2.40
    static constexpr Fxp kLateralStiffnessRear = Fxp::BuildRaw(0x0002CCCC);  // ~2.80
    static constexpr Fxp kLateralForceCapBase = Fxp::BuildRaw(0x00028000); // 2.50
    static constexpr Fxp kYawMomentGain = Fxp::BuildRaw(0x00007000);       // ~0.4375
    static constexpr Fxp kYawCoupling = Fxp::BuildRaw(0x000028F6);         // ~0.16
    static constexpr Fxp kYawRateResponse = Fxp::BuildRaw(0x00008000);     // 0.50
    static constexpr Fxp kMaxYawRateDegPerFrame = Fxp::BuildRaw(0x00024000); // 2.25 deg/frame
    static constexpr Fxp kArcadeYawRateDegPerFrame = Fxp::BuildRaw(0x00030000); // 3.0 deg/frame
    static constexpr Fxp kReverseArcadeYawRateDegPerFrame = Fxp::BuildRaw(0x00048000); // 4.5 deg/frame
    static constexpr Fxp kArcadeHighSpeedSteerLoss = Fxp::BuildRaw(0x0000599A); // ~0.35
    static constexpr Fxp kArcadeSteerAuthorityMin = Fxp::BuildRaw(0x0000A666);  // ~0.65
    static constexpr Fxp kDegToRad = Fxp::BuildRaw(0x00000478);            // 0.01745
    static constexpr Fxp kRadToDeg = Fxp::BuildRaw(0x00394BC7);            // 57.2958
    static constexpr Fxp kYawFromSpeedCoeff = Fxp::BuildRaw(0x00000600);   // ~0.0234
    static constexpr Fxp kYawDamping = Fxp::BuildRaw(0x00003000);          // 0.1875
    static constexpr Fxp kLateralCouplingCoeff = Fxp::BuildRaw(0x00000040);// ~0.0010
    static constexpr Fxp kLateralDampingCoeff = Fxp::BuildRaw(0x00008000); // 0.50
    static constexpr Fxp kCoastLateralDampingCoeff = Fxp::BuildRaw(0x0000B000); // 0.6875
    static constexpr Fxp kCoastYawDampingCoeff = Fxp::BuildRaw(0x0000A000);     // 0.625
    static constexpr Fxp kCoastResidualForwardCutoff = Fxp::BuildRaw(0x0001D1DF); // ~1.8198
    static constexpr Fxp kCoastResidualLateralCutoff = Fxp::BuildRaw(0x0001D1DF); // ~1.8198
    static constexpr Fxp kCoastResidualYawCutoff = Fxp::BuildRaw(0x00004000);     // 0.25 deg/frame
    static constexpr int16_t kCoastSteerCenterThreshold = 10;                      // percent
    static constexpr Fxp kCoastNoSlideSpeedThreshold = Fxp::BuildRaw(0x000AEB3C);  // ~10.9189
    static constexpr Fxp kCoastNoSlideSteerScale = Fxp::BuildRaw(0x00002000);      // 0.125
    static constexpr Fxp kCoastNoSlideLateralDamping = Fxp::BuildRaw(0x0000E000);   // 0.875
    static constexpr Fxp kCoastNoSlideYawDamping = Fxp::BuildRaw(0x0000D000);       // 0.8125
    static constexpr Fxp kCoastNoSlideLateralCutoff = Fxp::BuildRaw(0x00002000);    // 0.125
    static constexpr Fxp kCoastNoSlideYawCutoff = Fxp::BuildRaw(0x00002000);        // 0.125 deg/frame
    // Minimum |speed| to keep full steering while coasting (throttle released).
    // Below this and no steer: residual yaw kill / no-slide still apply.
    static constexpr Fxp kCoastSteerMinSpeed = Fxp::BuildRaw(0x0000C000);           // ~0.75 wu/frame
    // Residual kill when braking WITHOUT steer (straight-line stop).
    static constexpr Fxp kBrakeLateralDampingCoeff = Fxp::BuildRaw(0x0000D000); // ~0.8125
    static constexpr Fxp kBrakeYawDampingCoeff = Fxp::BuildRaw(0x0000C000);     // 0.75
    // Trail-brake (brake + steer while rolling): keep most commanded yaw.
    // High-speed understeer is handled by kBrakeSteerLoss * brakeSlip, not by
    // wiping yawRate every frame (that made "brake = no turn").
    static constexpr Fxp kBrakeSteerYawDampingCoeff = Fxp::BuildRaw(0x00002000); // 0.125
    static constexpr Fxp kBrakeSteerLateralDampingCoeff = Fxp::BuildRaw(0x00004000); // 0.25
    // Min steer gate while trail-braking at crawl (arcade: still turn into the hairpin).
    static constexpr Fxp kBrakeSteerMinGate = Fxp::BuildRaw(0x0000A666);        // ~0.65
    static constexpr Fxp kBrakeResidualLateralCutoff = Fxp::BuildRaw(0x0001D1DF); // ~1.8198
    static constexpr Fxp kBrakeResidualYawCutoff = Fxp::BuildRaw(0x00004000);     // 0.25 deg/frame
    // Brake skid / understeer model (high speed only):
    // reduce steer authority and add light lateral skid — arcade inertia, not sim.
    // Starts ~100 km/h, full effect ~226 km/h.
    static constexpr Fxp kBrakeSkidStartSpeed = Fxp::BuildRaw(0x001232B9);      // ~18.20 wu/f ~= 100 km/h
    static constexpr Fxp kBrakeSkidFullSpeed = Fxp::BuildRaw(0x00292F0F);       // ~41.18 wu/f ~= 226 km/h
    static constexpr Fxp kBrakeSkidBase = Fxp::BuildRaw(0x00003333);            // 0.20
    static constexpr Fxp kBrakeSkidGripGain = Fxp::BuildRaw(0x00008000);        // 0.50
    static constexpr Fxp kBrakeSkidLateralDampingRelease = Fxp::BuildRaw(0x0000599A); // ~0.35
    static constexpr Fxp kBrakeSkidYawDampingRelease = Fxp::BuildRaw(0x00004000);     // 0.25
    // Max fraction of steer authority lost under full high-speed brake (understeer).
    // 0.35 keeps ~65% turn so the car still rotates into the corner.
    static constexpr Fxp kBrakeSteerLoss = Fxp::BuildRaw(0x0000599A);           // ~0.35 (was 0.50)
    static constexpr int16_t kBrakeDriftMinSteerPercent = 12;
    static constexpr uint8_t kBrakeDriftEntryFrames = 10u;
    static constexpr Fxp kBrakeDriftDecelScale = Fxp::BuildRaw(0x00007333);      // 0.45
    static constexpr Fxp kBrakeDriftYawKick = Fxp::BuildRaw(0x0000C000);         // 0.75 deg/frame
    static constexpr Fxp kBrakeSkidLateralSpeedRatio = Fxp::BuildRaw(0x0000599A); // 0.35
    static constexpr Fxp kBrakeSkidLateralResponse = Fxp::BuildRaw(0x0000A000);   // 0.625
    static constexpr Fxp kBrakeSkidLateralDecay = Fxp::BuildRaw(0x00008000);      // 0.50
    static constexpr Fxp kBrakeSkidLateralCutoff = Fxp::BuildRaw(0x00002000);     // 0.125
    // Low-speed launch handling:
    // use a kinematic yaw model and suppress lateral slide until speed stabilizes.
    static constexpr Fxp kLaunchKinematicSpeedThreshold = Fxp::BuildRaw(0x001232B9); // ~18.1981
    static constexpr uint8_t kForwardLaunchLateralLockFrames = 2u;
    // Keep steering sign lock active a bit longer after standstill launch so
    // the car cannot briefly arc to the opposite side during kinematic->slip handoff.
    static constexpr Fxp kForwardSteerSignLockSpeedThreshold = Fxp::BuildRaw(0x001D1DF5); // ~29.1170
    static constexpr Fxp kLaunchPureForwardSpeedThreshold = Fxp::BuildRaw(0x0000E8F0); // ~0.9099
    static constexpr Fxp kLaunchCrawlSpeedThreshold = Fxp::BuildRaw(0x0000E8F0); // ~0.9099
    static constexpr Fxp kLaunchYawRateResponse = Fxp::BuildRaw(0x0000A000);         // 0.625
    // Zero-speed launch with steering:
    // immediate turn entry (no forced straight frame).
    static constexpr uint8_t kLaunchStraightFrameCount = 0u;
    static constexpr Fxp kLaunchStraightEntrySpeed = Fxp::BuildRaw(0x0009195C); // ~9.0991
    static constexpr Fxp kRideHeightOffset = Fxp::BuildRaw(-(1 << 14));    // -0.25
    static constexpr Fxp kFastProbeSpeedThreshold = Fxp::BuildRaw(0x00246572); // ~36.3963
    // Chassis spring speed caps. No normal terrain transition may snap Y.
    static constexpr Fxp kMaxYStepUpPerFrame = Fxp::BuildRaw(0x00100000);      // 16.0
    static constexpr Fxp kMaxYStepDownPerFrame = Fxp::BuildRaw(0x00100000);    // 16.0
    // Per-frame topology telemetry for camera/grade behavior.
    static constexpr Fxp kTopoDropYThreshold = Fxp::BuildRaw(0x00004000);      // 0.25
    static constexpr uint8_t kTopoDropHoldFrames = 12u;
    static constexpr Fxp kTopoGradeDeclineMin = Fxp::BuildRaw(0x00000C00);     // ~0.05 tan
    static constexpr bool kEnableSlopePathAssist = false;
    static constexpr Fxp kSlopeGravityPerFrame = Fxp::BuildRaw(0x00000400);
    static constexpr Fxp kSlopeTanMax = Fxp::BuildRaw(0x0000B333);            // ~0.70
    static constexpr Fxp kSlopeTanDeadzone = Fxp::BuildRaw(0x00000A00);       // ~0.04
    static constexpr uint8_t kGradeFilterShift = 2u;                          // 1/4 toward sample
    static constexpr bool kEnableGradePredictY = false;
    static constexpr Fxp kGradePredictYMax = Fxp::BuildRaw(0x00008000);       // 0.5
    static constexpr Fxp kStationaryYawLockSpeed = Fxp::BuildRaw(0x00005A00);
    static constexpr Fxp kSurfaceSampleDownBias = Fxp::BuildRaw(0x00008000);   // 0.5
    // CAR1 wheel rectangle in model/world units.
    static constexpr Fxp kProbeHalfWheelBase =
        Fxp::BuildRaw(ContactGeometry::kHalfWheelBaseRaw);
    static constexpr Fxp kProbeHalfTrack =
        Fxp::BuildRaw(ContactGeometry::kHalfTrackRaw);
    static constexpr Fxp kProbeFrontBase = kProbeHalfWheelBase;
    static constexpr Fxp kProbeFrontSpeedScale = Fxp::BuildRaw(0);
    static constexpr Fxp kProbeFrontMin = kProbeHalfWheelBase;
    static constexpr Fxp kProbeFrontMax = kProbeHalfWheelBase;
    static constexpr Fxp kProbeSlopeAssistSpeed = Fxp::BuildRaw(0x0007477D);
    static constexpr uint8_t kAuxProbeCadenceFrames = 2u;
    static constexpr uint8_t kGripProbeIntervalFrames =
        kEnableSaturnLowCostPhysics ? 6u : 2u;
    static constexpr uint8_t kSurfaceContactCadenceFrames =
        kEnableSaturnLowCostPhysics ? 6u : 1u;
    // 4-corner wheel plane (pitch + roll). Not reduced centerline.
    static constexpr bool kPreferReducedGroundProbe = false;
    // Saturn-safe path: one wheel diagonal per frame keeps two surface queries
    // while persistent per-corner springs reconstruct the four-contact plane.
    static constexpr bool kForceAxleCenterlineProbes = kEnableSaturnLowCostPhysics;
    static constexpr bool kEnableFourWheelPlaneProbes = true;
    static constexpr Fxp kWallCollisionRadius = Fxp::BuildRaw(0x0001599A);     // ~1.35
    static constexpr bool kEnableWallPlanarPush =
        Game::PhysicsFeatureFlags::kEnableWallCollisionRuntime;
    // Disabled on Saturn: 4 extra FindPlanarWallPush calls/frame (one per clip) exceed
    // the SH2 budget.
    static constexpr bool kEnableBodyClipPlanarReaction = !kEnableSaturnLowCostPhysics;
    // Surface query per body clip is too expensive on Saturn; wall-only clips still run.
    static constexpr bool kBodyClipQuerySurface = !kEnableSaturnLowCostPhysics;
    // Minimum wall push magnitude to trigger velocity cancellation (avoids noise on graze).
    static constexpr Fxp kWallPushVelocityCancelThreshold = Fxp::BuildRaw(0x00001999); // ~0.10
    static constexpr Fxp kWallImpactForwardDamping = Fxp::BuildRaw(0x0000A000); // 0.625
    static constexpr Fxp kWallImpactYawDamping = Fxp::BuildRaw(0x0000D000);     // 0.8125
    static constexpr Fxp kWallImpactStopCutoff = Fxp::BuildRaw(0x00026666);     // ~2.40
    static constexpr Fxp kWallSeparationSkin = Fxp::BuildRaw(0x00001000);       // 0.0625
    static constexpr Fxp kWallHullHalfLength = Fxp::BuildRaw(40 << 16);         // 40.0 (~2.03 m)
    static constexpr Fxp kWallHullHalfWidth = Fxp::BuildRaw(20 << 16);          // 20.0 (~1.02 m)
    static constexpr Fxp kWallHullProbeRadius = Fxp::BuildRaw(6 << 16);         // 6.0  (~0.31 m)
    static constexpr Fxp kBodyClipPenetrationBias = Fxp::BuildRaw(0x00000800);  // 0.03125
    static constexpr Fxp kBodyClipMaxDepth = Fxp::BuildRaw(0x00018000);         // 1.5
    static constexpr Fxp kBodyClipMinPlanarNormalAbs = Fxp::BuildRaw(0x00002000); // 0.125
    static constexpr Fxp kBodyClipWallRadius = Fxp::BuildRaw(0x0000D999);       // ~0.85
    static constexpr Fxp kBodyClipWallPushScale = Fxp::BuildRaw(0x0000C000);    // 0.75
    static constexpr Fxp kBodyClipMaxPushPerClip = Fxp::BuildRaw(0x0000CCCD);   // 0.80
    static constexpr std::array<BodyClip, 4> kBodyClips = {{
        { kProbeHalfWheelBase,  Fxp::BuildRaw(-kProbeHalfTrack.RawValue()), Fxp::BuildRaw(0x0000999A), Fxp::BuildRaw(0x00002000) },
        { kProbeHalfWheelBase,  kProbeHalfTrack,                              Fxp::BuildRaw(0x0000999A), Fxp::BuildRaw(0x00002000) },
        { Fxp::BuildRaw(-kProbeHalfWheelBase.RawValue()), Fxp::BuildRaw(-kProbeHalfTrack.RawValue()), Fxp::BuildRaw(0x0000999A), Fxp::BuildRaw(0x00002000) },
        { Fxp::BuildRaw(-kProbeHalfWheelBase.RawValue()), kProbeHalfTrack,    Fxp::BuildRaw(0x0000999A), Fxp::BuildRaw(0x00002000) }
    }};
    static constexpr Fxp kEdgeRecoverPush = Fxp::BuildRaw(0);                  // disabled (bias fix)
    static constexpr Fxp kMaxPlanarCorrectionPerFrame = Fxp::BuildRaw(0x00030000); // 3.0 — stronger ejection, still conservative
    static constexpr Fxp kNoSupportSpeedDamping = Fxp::BuildRaw(0x0007477D);   // ~7.2793
    static constexpr Fxp kEdgeForwardDamping = Fxp::BuildRaw(0x0000A000);      // 0.625
    static constexpr Fxp kEdgeLateralDamping = Fxp::BuildRaw(0x0000E000);      // 0.875
    static constexpr int8_t ClampForwardGear(int8_t gear)
    {
        if (gear < 1) return 1;
        if (gear > static_cast<int8_t>(kForwardGearCount)) return static_cast<int8_t>(kForwardGearCount);
        return gear;
    }

    static constexpr int8_t ClampSelectableGear(int8_t gear)
    {
        if (gear < kReverseGear) return kReverseGear;
        if (gear > static_cast<int8_t>(kForwardGearCount)) return static_cast<int8_t>(kForwardGearCount);
        return gear;
    }

    static constexpr size_t GearIndex(int8_t gear)
    {
        return static_cast<size_t>(ClampForwardGear(gear) - 1);
    }

    static constexpr Fxp GearAccelFor(int8_t gear)
    {
        return kGearAccelPerFrame[GearIndex(gear)];
    }

    static constexpr uint8_t GearBandTargetFramesFor(int8_t gear)
    {
        return kGearBandTargetFrames[GearIndex(gear)];
    }

    static constexpr int16_t GearBandStartSpeedKmhFor(int8_t gear)
    {
        const size_t index = GearIndex(gear);
        return (index == 0u) ? 0 : kGearTopSpeedKmh[index - 1u];
    }

    static constexpr int16_t GearTopSpeedKmhFor(int8_t gear)
    {
        return kGearTopSpeedKmh[GearIndex(gear)];
    }

    static constexpr int16_t GearLoadedMinRpmFor(int8_t gear)
    {
        return kGearLoadedMinRpm[GearIndex(gear)];
    }

    static constexpr int16_t GearMinTypicalSpeedKmhFor(int8_t gear)
    {
        return kGearMinTypicalSpeedKmh[GearIndex(gear)];
    }

    static constexpr int16_t GearLoadedMaxRpmFor(int8_t gear)
    {
        return kGearLoadedMaxRpm[GearIndex(gear)];
    }

    static constexpr int16_t GearShiftUpRpmFor(int8_t gear)
    {
        return kGearShiftUpRpm[GearIndex(gear)];
    }

    static constexpr int16_t GearShiftLandingMinRpmFor(int8_t gear)
    {
        return kGearShiftLandingMinRpm[GearIndex(gear)];
    }

    static constexpr int16_t GearShiftLandingMaxRpmFor(int8_t gear)
    {
        return kGearShiftLandingMaxRpm[GearIndex(gear)];
    }

    static constexpr int16_t GearShiftLandingDipRpmFor(int8_t gear)
    {
        return kGearShiftLandingDipRpm[GearIndex(gear)];
    }

    static constexpr uint16_t GearRpmRisePerFrameFor(int8_t gear)
    {
        return kGearRpmRisePerFrame[GearIndex(gear)];
    }

    static constexpr uint16_t GearRpmDropPerFrameFor(int8_t gear)
    {
        return kGearRpmDropPerFrame[GearIndex(gear)];
    }


    static constexpr Fxp GearHighSpeedAeroExtraScaleFor(int8_t gear)
    {
        return kGearHighSpeedAeroExtraScale[GearIndex(gear)];
    }

    static constexpr Fxp GearHighSpeedAccelMinScaleFor(int8_t gear)
    {
        return kGearHighSpeedAccelMinScale[GearIndex(gear)];
    }
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
    const int32_t kmhRaw = (speedAbs * Tunables::kKmhPerWorldUnitPerFrame).RawValue();
    const int32_t kmhRounded = (kmhRaw + 0x8000) >> 16;
    return static_cast<int16_t>(std::clamp<int32_t>(kmhRounded, 0, 32767));
}

inline int16_t BuildSignedSpeedKmh(const Fxp& forwardSpeed)
{
    const int16_t speedAbsKmh = BuildSpeedProxy(forwardSpeed);
    return (forwardSpeed.RawValue() < 0)
        ? static_cast<int16_t>(-speedAbsKmh)
        : speedAbsKmh;
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
    ioFrameState.debugGroundYLeft = 0;
    ioFrameState.debugGroundYRight = 0;
    ioFrameState.debugGroundYTarget = 0;
    ioFrameState.debugGroundYBody = 0;
    ioFrameState.debugGroundDY = 0;
    ioFrameState.debugGradeTanX100 = 0;
    ioFrameState.debugGroundYRearRaw = 0;
    ioFrameState.debugGroundYFrontRaw = 0;
    ioFrameState.debugGroundYLeftRaw = 0;
    ioFrameState.debugGroundYRightRaw = 0;
    ioFrameState.debugTopoDrop = 0u;
    ioFrameState.debugWheelSurfYFl = 0;
    ioFrameState.debugWheelSurfYFr = 0;
    ioFrameState.debugWheelSurfYRl = 0;
    ioFrameState.debugWheelSurfYRr = 0;
    ioFrameState.debugWheelDistFl = 0;
    ioFrameState.debugWheelDistFr = 0;
    ioFrameState.debugWheelDistRl = 0;
    ioFrameState.debugWheelDistRr = 0;
    ioFrameState.debugWheelResidualFlX256 = 0;
    ioFrameState.debugWheelResidualFrX256 = 0;
    ioFrameState.debugWheelResidualRlX256 = 0;
    ioFrameState.debugWheelResidualRrX256 = 0;
    ioFrameState.debugSteerDeg = 0;
    ioFrameState.debugYawRateDeg = 0;
    ioFrameState.debugYawStepDeg = 0;
    ioFrameState.debugEngineRpm = 0;
    ioFrameState.debugGear = 0;
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

inline void PublishAuthoritativeDrivetrain(GameplayFrameState& ioFrameState,
                                           int16_t gear,
                                           int16_t engineRpm,
                                           int16_t speedKmh)
{
    ioFrameState.carGear = gear;
    ioFrameState.carEngineRpm = engineRpm;
    ioFrameState.carSpeedKmh = speedKmh;

    ioFrameState.debugGear = gear;
    ioFrameState.debugEngineRpm = engineRpm;
    ioFrameState.debugSpeedKmh = speedKmh;
}

inline void PublishShiftTelemetry(GameplayFrameState& ioFrameState,
                                  int16_t rpmBefore,
                                  int16_t rpmAfter,
                                  uint8_t framesVisible)
{
    ioFrameState.carShiftRpmBefore = rpmBefore;
    ioFrameState.carShiftRpmAfter = rpmAfter;
    ioFrameState.carShiftFrames = framesVisible;
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
