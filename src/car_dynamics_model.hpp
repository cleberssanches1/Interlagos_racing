#pragma once

#include "car_physics_shared.hpp"

namespace Game::CarPhysics
{
class DynamicsModel
{
public:
    static void IntegratePlanar(GameplayFrameState& ioFrameState,
                                DynamicsState& ioState,
                                Vector3D& ioCarWorldPosition,
                                int32_t& ioCarYawDeg,
                                FrameStepOutput& outStep)
    {
        const Fxp throttleNorm = NormalizePercent(ioFrameState.throttle);
        const Fxp steerNorm = NormalizePercent(ioFrameState.steering);
        const Fxp gripScale = Clamp(ioState.surfaceGripScale,
                                    Tunables::kGripScaleFallback,
                                    Tunables::kGripScaleAsphalt);
        const bool hasSteerCommand = (ioFrameState.steering != 0);
        const bool hasThrottleDriveIntent =
            (ioFrameState.throttle > 0) &&
            !ioFrameState.braking;
        const int16_t steerAbsPercent =
            static_cast<int16_t>((ioFrameState.steering < 0) ? -ioFrameState.steering : ioFrameState.steering);

        // Convert runtime world-units/frame to real km/h using the PATH-calibrated scale.
        const int16_t speedKmhSigned = BuildSignedSpeedKmh(ioState.forwardSpeed);
        const int16_t speedKmhAbs = static_cast<int16_t>((speedKmhSigned < 0) ? -speedKmhSigned : speedKmhSigned);

        const auto computeRpmForGear = [&](uint8_t gear) -> int16_t
        {
            const int16_t gearTopKmh =
                (gear == Tunables::kReverseGear)
                    ? Tunables::kReverseTopSpeedKmh
                    : Tunables::GearTopSpeedKmhFor(gear);
            int32_t rpm = Tunables::kEngineIdleRpm;
            if (gearTopKmh > 0)
            {
                rpm = (static_cast<int32_t>(speedKmhAbs) * Tunables::kEngineMaxRpm) / gearTopKmh;
                if (rpm < Tunables::kEngineIdleRpm) rpm = Tunables::kEngineIdleRpm;
                if (rpm > Tunables::kEngineMaxRpm) rpm = Tunables::kEngineMaxRpm;
            }
            return static_cast<int16_t>(rpm);
        };

        ioState.gear = Tunables::ClampSelectableGear(ioState.gear);
        if (ioFrameState.shiftDownRequested)
        {
            const uint8_t nextGear =
                (ioState.gear > Tunables::kReverseGear)
                    ? static_cast<uint8_t>(ioState.gear - 1u)
                    : Tunables::kReverseGear;
            const bool reverseRequest = (nextGear == Tunables::kReverseGear);
            const bool allowShift =
                reverseRequest
                    ? (speedKmhAbs <= Tunables::kReverseShiftMaxKmh)
                    : (computeRpmForGear(nextGear) <= Tunables::kEngineMaxRpm);
            if (allowShift)
            {
                ioState.gear = nextGear;
            }
        }
        if (ioFrameState.shiftUpRequested)
        {
            const bool leavingReverse = (ioState.gear == Tunables::kReverseGear);
            const bool allowShift =
                leavingReverse
                    ? (speedKmhAbs <= Tunables::kReverseShiftMaxKmh)
                    : (ioState.gear < Tunables::kForwardGearCount);
            if (allowShift && ioState.gear < Tunables::kForwardGearCount)
            {
                ++ioState.gear;
            }
        }
        ioState.gear = Tunables::ClampSelectableGear(ioState.gear);
        const bool isReverseGearSelected = (ioState.gear == Tunables::kReverseGear);
        const bool hasForwardDriveIntent = hasThrottleDriveIntent && !isReverseGearSelected;
        const bool hasReverseDriveIntent = hasThrottleDriveIntent && isReverseGearSelected;
        const bool reverseMotionActive =
            isReverseGearSelected &&
            (ioState.forwardSpeed < Fxp::BuildRaw(0));
        const bool isIntentionalReverse =
            isReverseGearSelected &&
            (reverseMotionActive || hasReverseDriveIntent);
        const bool brakeDriftEntry =
            Tunables::kEnableSaturnLowCostPhysics &&
            ioFrameState.braking &&
            !isReverseGearSelected &&
            !ioState.wasBraking &&
            (ioState.forwardSpeed > Fxp::BuildRaw(0)) &&
            (ioState.forwardSpeed.Abs() >= Tunables::kBrakeSkidStartSpeed) &&
            (steerAbsPercent >= Tunables::kBrakeDriftMinSteerPercent);

        if (brakeDriftEntry)
        {
            ioState.brakeDriftFrames = Tunables::kBrakeDriftEntryFrames;
        }

        ioState.engineRpm = computeRpmForGear(ioState.gear);

        if (Tunables::kAutomaticGearboxEnabled &&
            !ioFrameState.braking &&
            ioFrameState.throttle > 0 &&
            !isReverseGearSelected)
        {
            // Automatic F1-style progression:
            // quick short lower gears, then progressively longer upper gears.
            if (ioState.engineRpm >= Tunables::kEngineUpShiftRpm &&
                ioState.gear < Tunables::kForwardGearCount)
            {
                ++ioState.gear;
                ioState.engineRpm = computeRpmForGear(ioState.gear);
            }
        }

        if (ioFrameState.braking)
        {
            Fxp brakeDecel = Tunables::kBrakeDecelPerFrame;
            if constexpr (Tunables::kEnableSaturnLowCostPhysics)
            {
                if (ioState.brakeDriftFrames > 0u)
                {
                    brakeDecel = brakeDecel * Tunables::kBrakeDriftDecelScale;
                }
            }
            if (ioState.forwardSpeed > Fxp::BuildRaw(0))
            {
                ioState.forwardSpeed -= brakeDecel;
            }
            else if (ioState.forwardSpeed < Fxp::BuildRaw(0))
            {
                ioState.forwardSpeed += brakeDecel;
            }
            else
            {
                ioState.forwardSpeed = Fxp::BuildRaw(0);
            }
        }
        else if (throttleNorm > Fxp::BuildRaw(0))
        {
            const bool wasReversing = (ioState.forwardSpeed < Fxp::BuildRaw(0));
            if (hasReverseDriveIntent)
            {
                if (ioState.forwardSpeed > Fxp::BuildRaw(0))
                {
                    ioState.forwardSpeed -= Tunables::kBrakeDecelPerFrame;
                }
                else
                {
                    ioState.forwardSpeed -= throttleNorm * Tunables::kReverseAccelPerFrame;
                }
            }
            else
            {
                const Fxp gearAccel = Tunables::GearAccelFor(ioState.gear);
                ioState.forwardSpeed += throttleNorm * gearAccel;
                if (ioState.forwardSpeed < Fxp::BuildRaw(0))
                {
                    ioState.forwardSpeed += Tunables::kBrakeDecelPerFrame;
                }
            }
            // Transition from reverse to forward:
            // remove sideways/yaw residue to prevent a lateral slide before moving ahead.
            if (wasReversing)
            {
                ioState.lateralSpeed -= ioState.lateralSpeed * Fxp::BuildRaw(0x00010000); // 1.0
                ioState.yawRateDegPerFrame -= ioState.yawRateDegPerFrame * Fxp::BuildRaw(0x00010000); // 1.0
                ioState.lateralSpeed = Fxp::BuildRaw(0);
                ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
                ioState.yawAccumulatorDegRaw = 0;
            }
        }

        outStep.speedAbs = ioState.forwardSpeed.Abs();
        const bool hasForwardDriveCommand =
            hasForwardDriveIntent &&
            (ioState.forwardSpeed >= Fxp::BuildRaw(0));
        if (!hasForwardDriveCommand || !hasSteerCommand)
        {
            ioState.steerLaunchArmed = true;
        }
        if (ioState.steerLaunchArmed &&
            hasForwardDriveCommand &&
            hasSteerCommand &&
            (outStep.speedAbs < Tunables::kLaunchStraightEntrySpeed))
        {
            ioState.launchStraightFrames = Tunables::kLaunchStraightFrameCount;
            ioState.steerLaunchArmed = false;
        }
        const Fxp aeroDrag = outStep.speedAbs * ioState.forwardSpeed * Tunables::kAeroDragCoeff;
        ioState.forwardSpeed -= aeroDrag;
        ioState.forwardSpeed -= ioState.forwardSpeed * Tunables::kRollingDragCoeff;

        if (ioFrameState.throttle == 0 && !ioFrameState.braking)
        {
            ApplyCoastDamping(ioState);
        }

        ioState.forwardSpeed = Clamp(ioState.forwardSpeed,
                                     Fxp::BuildRaw(-Tunables::kMaxReverseSpeed.RawValue()),
                                     Tunables::kMaxForwardSpeed);
        if (ioFrameState.braking &&
            ioState.forwardSpeed > Fxp::BuildRaw(0) &&
            ioState.forwardSpeed.Abs() < Tunables::kBrakeStopSpeedThreshold)
        {
            ioState.forwardSpeed = Fxp::BuildRaw(0);
        }

        const bool coastNoSlideMode =
            (ioFrameState.throttle == 0) &&
            !ioFrameState.braking &&
            (ioState.forwardSpeed.Abs() < Tunables::kCoastNoSlideSpeedThreshold);
        const Fxp steerScale =
            coastNoSlideMode ? Tunables::kCoastNoSlideSteerScale : Fxp::BuildRaw(1 << 16);
        const Fxp targetSteerDeg = (steerNorm * steerScale) * Tunables::kMaxSteerDeg;
        const bool steerCrossesZero =
            ((ioFrameState.steering < 0) && (ioState.steerDeg > Fxp::BuildRaw(0))) ||
            ((ioFrameState.steering > 0) && (ioState.steerDeg < Fxp::BuildRaw(0)));
        const bool launchSteerSnap =
            (ioFrameState.steering != 0) &&
            (ioFrameState.throttle > 0 || ioFrameState.braking) &&
            ((ioState.forwardSpeed.Abs() < Tunables::kForwardSteerLaunchSpeedThreshold) ||
             (steerCrossesZero && outStep.speedAbs < Tunables::kLaunchKinematicSpeedThreshold));
        if (launchSteerSnap)
        {
            // Remove cross-zero lag at launch so yaw cannot start on the wrong side.
            ioState.steerDeg = targetSteerDeg;
        }
        else
        {
            ioState.steerDeg += (targetSteerDeg - ioState.steerDeg) * Tunables::kSteerResponse;
        }

        const Fxp speedRatio =
            Clamp(outStep.speedAbs / Tunables::kMaxForwardSpeed,
                  Fxp::BuildRaw(0),
                  Fxp::BuildRaw(1 << 16));
        const Fxp steerAbsNorm = steerNorm.Abs();
        const Fxp gripLoss =
            Clamp(Fxp::BuildRaw(1 << 16) - gripScale, Fxp::BuildRaw(0), Fxp::BuildRaw(1 << 16));
        Fxp brakeSlip = Fxp::BuildRaw(0);
        if (ioFrameState.braking && !isIntentionalReverse)
        {
            const Fxp brakeSpeedFactor =
                Clamp((outStep.speedAbs - Tunables::kBrakeSkidStartSpeed) /
                          (Tunables::kBrakeSkidFullSpeed - Tunables::kBrakeSkidStartSpeed),
                      Fxp::BuildRaw(0),
                      Fxp::BuildRaw(1 << 16));
            const Fxp brakeGripFactor =
                Tunables::kBrakeSkidBase + (gripLoss * Tunables::kBrakeSkidGripGain);
            brakeSlip = (brakeSpeedFactor * steerAbsNorm) * brakeGripFactor;
            brakeSlip = Clamp(brakeSlip, Fxp::BuildRaw(0), Fxp::BuildRaw(1 << 16));
        }
        // No in-place rotation: steering authority fades out near zero speed.
        const Fxp steerSpeedGate =
            Clamp((outStep.speedAbs - Fxp::BuildRaw(0x00004000)) / Fxp::BuildRaw(0x0000C000),
                  Fxp::BuildRaw(0),
                  Fxp::BuildRaw(1 << 16));
        Fxp steerAuthority = Fxp::BuildRaw(1 << 16) - (speedRatio * Tunables::kHighSpeedSteerLoss * gripScale);
        if (steerAuthority < Tunables::kSteerAuthorityMin)
        {
            steerAuthority = Tunables::kSteerAuthorityMin;
        }
        // Keep minimum steering authority while user is actively commanding
        // accel or brake/reverse, so direction can be changed repeatedly in reverse.
        Fxp steerGate = steerSpeedGate;
        if (ioFrameState.braking || ioFrameState.throttle > 0)
        {
            const Fxp kCommandSteerMinGate = Fxp::BuildRaw(0x0000599A); // ~0.35
            if (steerGate < kCommandSteerMinGate) steerGate = kCommandSteerMinGate;
        }
        if (isIntentionalReverse)
        {
            steerGate = Fxp::BuildRaw(1 << 16);
        }
        steerAuthority = steerAuthority * steerGate;
        if (ioFrameState.braking && !isIntentionalReverse)
        {
            const Fxp brakeSteerFactor =
                Fxp::BuildRaw(1 << 16) - (brakeSlip * Tunables::kBrakeSteerLoss);
            steerAuthority = steerAuthority * brakeSteerFactor;
        }

        // Match gameplay convention directly:
        // negative steering => left turn, positive steering => right turn.
        // Reverse steering: yaw response must be inverted when moving backwards.
        // Only invert during intentional reverse (braking held) — not during reverse→forward
        // transition where forwardSpeed is briefly negative but throttle is pressed.
        Fxp steerEffDeg = ioState.steerDeg * steerAuthority;
        if (reverseMotionActive)
        {
            steerEffDeg = Fxp::BuildRaw(-steerEffDeg.RawValue());
        }

        const bool forceStraightLaunch =
            hasForwardDriveCommand &&
            hasSteerCommand &&
            (ioState.launchStraightFrames > 0u);

        // Use hasForwardDriveIntent (not hasForwardDriveCommand) so kinematic fires even during
        // the reverse→forward transition (forwardSpeed briefly < 0), preventing the slip model
        // from generating wrong-direction yaw before forwardSpeed crosses zero.
        const bool useLowSpeedKinematic =
            hasForwardDriveIntent &&
            hasSteerCommand &&
            (outStep.speedAbs < Tunables::kLaunchKinematicSpeedThreshold);

        if (useLowSpeedKinematic || Tunables::kEnableSaturnLowCostPhysics)
        {
            // Arcade kinematic: yaw rate = steer x maxYaw, lateralSpeed = 0.
            // Direct mapping means the sign is always correct from frame 1 — no guards needed.
            ioState.lateralSpeed = Fxp::BuildRaw(0);
            ioState.wasKinematic = true;
            outStep.wasKinematicMode = true;
            if constexpr (Tunables::kEnableSaturnLowCostPhysics)
            {
                Fxp steerArcadeNorm = ioState.steerDeg * Tunables::kInvMaxSteerDeg;
                if (reverseMotionActive)
                {
                    steerArcadeNorm = Fxp::BuildRaw(-steerArcadeNorm.RawValue());
                }
                Fxp arcadeSteerAuthority =
                    Fxp::BuildRaw(1 << 16) - (speedRatio * Tunables::kArcadeHighSpeedSteerLoss);
                if (arcadeSteerAuthority < Tunables::kArcadeSteerAuthorityMin)
                {
                    arcadeSteerAuthority = Tunables::kArcadeSteerAuthorityMin;
                }
                if (isIntentionalReverse)
                {
                    arcadeSteerAuthority = Fxp::BuildRaw(1 << 16);
                }
                else if (ioFrameState.braking)
                {
                    const Fxp brakeSteerFactor =
                        Fxp::BuildRaw(1 << 16) - (brakeSlip * Tunables::kBrakeSteerLoss);
                    arcadeSteerAuthority = arcadeSteerAuthority * brakeSteerFactor;
                }
                const Fxp arcadeYawRate =
                    isIntentionalReverse
                        ? Tunables::kReverseArcadeYawRateDegPerFrame
                        : Tunables::kArcadeYawRateDegPerFrame;
                const Fxp yawTarget =
                    (steerArcadeNorm * arcadeYawRate) * arcadeSteerAuthority;
                ioState.yawRateDegPerFrame +=
                    (Fxp::BuildRaw(-yawTarget.RawValue()) - ioState.yawRateDegPerFrame) *
                    Tunables::kYawRateResponse;
            }
            else
            {
                const Fxp kKinematicYawSpeedLoss = Fxp::BuildRaw(0x00010000); // 1.0
                const Fxp kKinematicYawMinScale = Fxp::BuildRaw(0x00008000); // 0.5
                Fxp kinematicYawScale = Fxp::BuildRaw(1 << 16) - (speedRatio * kKinematicYawSpeedLoss);
                if (kinematicYawScale < kKinematicYawMinScale)
                {
                    kinematicYawScale = kKinematicYawMinScale;
                }

                const Fxp yawKinematic =
                    (steerNorm * Tunables::kMaxYawRateDegPerFrame) * kinematicYawScale;
                // Sign convention preserved:
                // steering < 0 (left) -> positive yaw rate.
                ioState.yawRateDegPerFrame = Fxp::BuildRaw(-yawKinematic.RawValue());
            }
        }
        else
        {
            // Arcade: zero all lateral state on kinematic exit to prevent slip model
            // from using the accumulated yaw rate as a lateral impulse (the "slide" bug).
            // If kinematic exited because speed crossed the threshold, keep yawRate
            // for smooth transition; otherwise the exit was due to input release — zero it.
            const bool prevWasKinematic = ioState.wasKinematic;
            ioState.wasKinematic = false;
            if (prevWasKinematic)
            {
                ioState.lateralSpeed = Fxp::BuildRaw(0);
                if (outStep.speedAbs < Tunables::kLaunchKinematicSpeedThreshold)
                {
                    // Input was released while at low speed: clear yaw residue so the
                    // slip model cannot convert it into lateral slide.
                    ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
                    ioState.yawAccumulatorDegRaw = 0;
                }
                else
                {
                    // Speed-cross exit from kinematic: keep yaw continuity but
                    // reduce first-frame lateral impulse.
                    ioState.yawRateDegPerFrame -=
                        ioState.yawRateDegPerFrame * Fxp::BuildRaw(0x00004000); // 0.25
                }
            }

            // Two-axle slip model inspired by TORCS/VDrift, adapted for fixed-point Saturn:
            // alpha_f ~= delta - (vy + lf*r)/|vx|, alpha_r ~= -(vy - lr*r)/|vx|
            // Fy = clamp(-Ca * alpha, +/- FyCap)
            const Fxp steerEffRad = steerEffDeg * Tunables::kDegToRad;
            const Fxp yawRateRadPerFrame = ioState.yawRateDegPerFrame * Tunables::kDegToRad;
            const Fxp vxAbs = ioState.forwardSpeed.Abs();
            const Fxp slipDenom = (vxAbs > Tunables::kSlipDenomMin) ? vxAbs : Tunables::kSlipDenomMin;

            const Fxp vyFront = ioState.lateralSpeed + (yawRateRadPerFrame * Tunables::kWheelbaseFront);
            const Fxp vyRear = ioState.lateralSpeed - (yawRateRadPerFrame * Tunables::kWheelbaseRear);

            const Fxp alphaFront = steerEffRad - (vyFront / slipDenom);
            const Fxp alphaRear = Fxp::BuildRaw(0) - (vyRear / slipDenom);

            const Fxp fyCap = Tunables::kLateralForceCapBase * gripScale;
            const Fxp fyFrontRaw = Fxp::BuildRaw(-((Tunables::kLateralStiffnessFront * alphaFront).RawValue()));
            const Fxp fyRearRaw = Fxp::BuildRaw(-((Tunables::kLateralStiffnessRear * alphaRear).RawValue()));
            const Fxp fyFront = SaturateSigned(fyFrontRaw, fyCap);
            const Fxp fyRear = SaturateSigned(fyRearRaw, fyCap);

            Fxp lateralAccel = fyFront + fyRear;
            const Fxp yawCoupling = ioState.forwardSpeed * yawRateRadPerFrame * Tunables::kYawCoupling;
            lateralAccel += yawCoupling;
            ioState.lateralSpeed += lateralAccel;
            // 16.2: strengthen damping at low speed to suppress launch oscillation
            // while keeping high-speed slip behavior close to current tuning.
            const Fxp kLateralDampingLowSpeedBoost = Fxp::BuildRaw(0x00002000); // 0.125
            const Fxp lowSpeedBlend = Fxp::BuildRaw(1 << 16) - speedRatio;
            Fxp lateralDampingCoeff =
                Tunables::kLateralDampingCoeff + (lowSpeedBlend * kLateralDampingLowSpeedBoost);
            if (lateralDampingCoeff > Tunables::kCoastLateralDampingCoeff)
            {
                lateralDampingCoeff = Tunables::kCoastLateralDampingCoeff;
            }
            ioState.lateralSpeed -= ioState.lateralSpeed * lateralDampingCoeff;

            const Fxp yawMoment =
                (fyFront * Tunables::kWheelbaseFront) - (fyRear * Tunables::kWheelbaseRear);
            const Fxp targetYawRateDegPerFrame =
                (yawMoment * Tunables::kYawMomentGain) * Tunables::kRadToDeg;

            ioState.yawRateDegPerFrame +=
                (targetYawRateDegPerFrame - ioState.yawRateDegPerFrame) * Tunables::kYawRateResponse;
            ioState.yawRateDegPerFrame -= ioState.yawRateDegPerFrame * Tunables::kYawDamping;
        }

        // Keep steering direction stable while accelerating in a turn.
        // This prevents right/left sign inversions injected by slip transients
        // after the standstill launch phase.
        const bool forwardLaunchSignLock =
            hasForwardDriveIntent &&
            hasSteerCommand;
        if (forwardLaunchSignLock)
        {
            // Keep launch sign strictly symmetric.
            // Coord convention: yaw increasing = left turn.
            // Left steer (steering < 0) should produce positive yaw rate.
            // Clamp any negative residue that would cause a right-turn artifact.
            if (ioFrameState.steering < 0)
            {
                if (ioState.yawRateDegPerFrame < Fxp::BuildRaw(0))
                {
                    ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
                }
                if (ioState.yawAccumulatorDegRaw < 0)
                {
                    ioState.yawAccumulatorDegRaw = 0;
                }
            }
            else if (ioFrameState.steering > 0)
            {
                if (ioState.yawRateDegPerFrame > Fxp::BuildRaw(0))
                {
                    ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
                }
                if (ioState.yawAccumulatorDegRaw > 0)
                {
                    ioState.yawAccumulatorDegRaw = 0;
                }
            }
        }

        // Launch stabilization (symmetric L/R):
        // 1) first frames after standstill launch: hard-lock lateral drift.
        // 2) next low-speed frames: strong damping as soft fallback.
        const bool lowSpeedForwardSteerLaunch =
            hasForwardDriveIntent &&
            hasSteerCommand &&
            (outStep.speedAbs < Tunables::kForwardSteerSignLockSpeedThreshold);
        if (lowSpeedForwardSteerLaunch)
        {
            if (ioState.forwardLaunchLateralLockFrames > 0u)
            {
                ioState.lateralSpeed = Fxp::BuildRaw(0);
                --ioState.forwardLaunchLateralLockFrames;
            }
            else
            {
                ioState.lateralSpeed -= ioState.lateralSpeed * Fxp::BuildRaw(0x0000C000); // 0.75
                if (ioState.lateralSpeed.Abs() < Fxp::BuildRaw(0x00002000))
                {
                    ioState.lateralSpeed = Fxp::BuildRaw(0);
                }
            }
        }
        else
        {
            ioState.forwardLaunchLateralLockFrames = 0u;
        }

        // Hard safety: if almost stopped and not accelerating or braking, do not rotate.
        if (outStep.speedAbs < Fxp::BuildRaw(0x00006000) &&
            ioFrameState.throttle == 0 &&
            !ioFrameState.braking)
        {
            ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
            ioState.yawAccumulatorDegRaw = 0;
        }

        if (ioFrameState.throttle == 0 && !ioFrameState.braking)
        {
            ioState.lateralSpeed -= ioState.lateralSpeed * Tunables::kCoastLateralDampingCoeff;
            ioState.yawRateDegPerFrame -= ioState.yawRateDegPerFrame * Tunables::kCoastYawDampingCoeff;
            if (coastNoSlideMode)
            {
                ioState.lateralSpeed -= ioState.lateralSpeed * Tunables::kCoastNoSlideLateralDamping;
                ioState.yawRateDegPerFrame -= ioState.yawRateDegPerFrame * Tunables::kCoastNoSlideYawDamping;
                if (ioState.lateralSpeed.Abs() < Tunables::kCoastNoSlideLateralCutoff)
                {
                    ioState.lateralSpeed = Fxp::BuildRaw(0);
                }
                if (ioState.yawRateDegPerFrame.Abs() < Tunables::kCoastNoSlideYawCutoff)
                {
                    ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
                    ioState.yawAccumulatorDegRaw = 0;
                }
            }

            const int16_t steerAbs =
                static_cast<int16_t>((ioFrameState.steering < 0) ? -ioFrameState.steering : ioFrameState.steering);
            const Fxp forwardAbs = ioState.forwardSpeed.Abs();
            if (forwardAbs < Tunables::kCoastStopSpeedThreshold &&
                steerAbs <= Tunables::kCoastSteerCenterThreshold)
            {
                if (forwardAbs < Tunables::kCoastResidualForwardCutoff)
                {
                    ioState.forwardSpeed = Fxp::BuildRaw(0);
                }
                if (ioState.lateralSpeed.Abs() < Tunables::kCoastResidualLateralCutoff)
                {
                    ioState.lateralSpeed = Fxp::BuildRaw(0);
                }
                if (ioState.yawRateDegPerFrame.Abs() < Tunables::kCoastResidualYawCutoff)
                {
                    ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
                    ioState.yawAccumulatorDegRaw = 0;
                }
            }
        }

        if (ioFrameState.braking)
        {
            Fxp brakeLateralDamping =
                Tunables::kBrakeLateralDampingCoeff -
                (brakeSlip * Tunables::kBrakeSkidLateralDampingRelease);
            Fxp brakeYawDamping =
                Tunables::kBrakeYawDampingCoeff -
                (brakeSlip * Tunables::kBrakeSkidYawDampingRelease);
            const Fxp kMinBrakeDamping = Fxp::BuildRaw(0x00004000); // 0.25
            if (brakeLateralDamping < kMinBrakeDamping) brakeLateralDamping = kMinBrakeDamping;
            if (brakeYawDamping < kMinBrakeDamping) brakeYawDamping = kMinBrakeDamping;
            ioState.lateralSpeed -= ioState.lateralSpeed * brakeLateralDamping;
            ioState.yawRateDegPerFrame -= ioState.yawRateDegPerFrame * brakeYawDamping;
            if (ioState.forwardSpeed == Fxp::BuildRaw(0) &&
                ioState.lateralSpeed.Abs() < Tunables::kBrakeResidualLateralCutoff &&
                ioState.yawRateDegPerFrame.Abs() < Tunables::kBrakeResidualYawCutoff)
            {
                ioState.lateralSpeed = Fxp::BuildRaw(0);
                ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
                ioState.yawAccumulatorDegRaw = 0;
            }
        }

        if constexpr (Tunables::kEnableSaturnLowCostPhysics)
        {
            if (ioFrameState.braking)
            {
                Fxp targetBrakeLateral = Fxp::BuildRaw(0);
                if (ioFrameState.steering != 0)
                {
                    const Fxp skidLateralMagnitude =
                        (outStep.speedAbs * brakeSlip) * Tunables::kBrakeSkidLateralSpeedRatio;
                    // Slide goes to the outside of the turn:
                    // left steer -> positive local-right drift, right steer -> negative.
                    if (ioFrameState.steering < 0)
                    {
                        targetBrakeLateral = skidLateralMagnitude;
                    }
                    else
                    {
                        targetBrakeLateral = Fxp::BuildRaw(-skidLateralMagnitude.RawValue());
                    }
                }
                if (brakeDriftEntry)
                {
                    if (ioFrameState.steering < 0)
                    {
                        ioState.yawRateDegPerFrame += Tunables::kBrakeDriftYawKick;
                    }
                    else if (ioFrameState.steering > 0)
                    {
                        ioState.yawRateDegPerFrame -= Tunables::kBrakeDriftYawKick;
                    }
                }
                ioState.lateralSpeed +=
                    (targetBrakeLateral - ioState.lateralSpeed) *
                    Tunables::kBrakeSkidLateralResponse;
            }
            else
            {
                ioState.lateralSpeed -=
                    ioState.lateralSpeed * Tunables::kBrakeSkidLateralDecay;
                if (ioState.lateralSpeed.Abs() < Tunables::kBrakeSkidLateralCutoff)
                {
                    ioState.lateralSpeed = Fxp::BuildRaw(0);
                }
            }
        }

        // When steering is released, aggressively damp yaw and recenter steer
        // to prevent endless spin / pivoting in place.
        if (ioFrameState.steering == 0)
        {
            ioState.yawRateDegPerFrame =
                ioState.yawRateDegPerFrame * Fxp::BuildRaw(0x00006000); // 0.375
            ioState.steerDeg = ioState.steerDeg * Fxp::BuildRaw(0x00008000); // 0.5
            if (ioState.yawRateDegPerFrame.Abs() < Fxp::BuildRaw(0x00008000)) // ~0.5 deg/frame
            {
                ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
            }
            if (ioState.steerDeg.Abs() < Fxp::BuildRaw(0x00010000)) // ~1 deg
            {
                ioState.steerDeg = Fxp::BuildRaw(0);
            }
        }

        ioState.yawRateDegPerFrame = Clamp(ioState.yawRateDegPerFrame,
                                           Fxp::BuildRaw(-Tunables::kMaxYawRateDegPerFrame.RawValue()),
                                           Tunables::kMaxYawRateDegPerFrame);

        // Prevent spin-in-place when throttle/steer are released near standstill.
        if (outStep.speedAbs < Fxp::BuildRaw(0x00008000) && // ~0.5 world units/frame
            ioFrameState.throttle == 0 &&
            !ioFrameState.braking &&
            ioState.steerDeg.Abs() < Fxp::BuildRaw(0x00008000)) // ~0.5 deg
        {
            ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
            ioState.yawAccumulatorDegRaw = 0;
        }

        // Explicit Euler integration for heading/position:
        // movement uses heading at frame start; yaw update affects next frame.
        const int32_t motionYawDegRaw =
            (static_cast<int32_t>(ioCarYawDeg) << 16) + ioState.yawAccumulatorDegRaw;
        // Preserve sub-degree yaw to keep steering responsive at low speeds.
        ioState.yawAccumulatorDegRaw += ioState.yawRateDegPerFrame.RawValue();
        // Use symmetric truncation (towards zero) to avoid signed-shift bias:
        // tiny negative rates must not become -1 degree immediately.
        int32_t yawStepDeg = ioState.yawAccumulatorDegRaw / (1 << 16);
        if (forwardLaunchSignLock)
        {
            // Left steer expects positive yaw step; clamp wrong-sign (negative) steps.
            if (ioFrameState.steering < 0 && yawStepDeg < 0)
            {
                yawStepDeg = 0;
                ioState.yawAccumulatorDegRaw = 0;
            }
            else if (ioFrameState.steering > 0 && yawStepDeg > 0)
            {
                yawStepDeg = 0;
                ioState.yawAccumulatorDegRaw = 0;
            }
        }
        ioState.yawAccumulatorDegRaw -= (yawStepDeg * (1 << 16));
        ioCarYawDeg = NormalizeYaw(ioCarYawDeg + yawStepDeg);
        outStep.yawStepDeg = static_cast<int16_t>(std::clamp<int32_t>(yawStepDeg, -32768, 32767));

        // Use fractional yaw at frame start for movement vector so turning does not
        // inject lateral displacement before forward launch motion.
        const auto yawAngle =
            SRL::Math::Types::Angle::FromDegrees(Fxp::BuildRaw(motionYawDegRaw));
        outStep.sinYaw = SRL::Math::Trigonometry::Sin(yawAngle);
        outStep.cosYaw = SRL::Math::Trigonometry::Cos(yawAngle);

        const Fxp negCosYaw = Fxp::BuildRaw(-outStep.cosYaw.RawValue());
        outStep.planarDx = (outStep.sinYaw * ioState.forwardSpeed) + (outStep.cosYaw * ioState.lateralSpeed);
        outStep.planarDz = (negCosYaw * ioState.forwardSpeed) + (outStep.sinYaw * ioState.lateralSpeed);
        ioCarWorldPosition.X += outStep.planarDx;
        ioCarWorldPosition.Z += outStep.planarDz;

        if (forceStraightLaunch && ioState.launchStraightFrames > 0u)
        {
            --ioState.launchStraightFrames;
        }
        if (ioState.brakeDriftFrames > 0u)
        {
            --ioState.brakeDriftFrames;
        }
        ioState.wasBraking = ioFrameState.braking;

        ioFrameState.debugSteerDeg = FxpToDebugInt(ioState.steerDeg);
        ioFrameState.debugYawRateDeg = FxpToDebugInt(ioState.yawRateDegPerFrame);
        ioFrameState.debugYawStepDeg = static_cast<int16_t>(outStep.yawStepDeg);
        ioFrameState.debugEngineRpm = ioState.engineRpm;
        ioFrameState.debugGear =
            (ioState.gear == Tunables::kReverseGear)
                ? static_cast<int16_t>(-1)
                : static_cast<int16_t>(ioState.gear);
        ioFrameState.debugSpeedKmh = speedKmhAbs;
        ioFrameState.debugPlanarDx = FxpToDebugInt(outStep.planarDx);
        ioFrameState.debugPlanarDz = FxpToDebugInt(outStep.planarDz);
    }

    static void ApplyNoSupportRecovery(DynamicsState& ioState)
    {
        ioState.forwardSpeed -= Tunables::kNoSupportSpeedDamping;
        if (ioState.forwardSpeed < Fxp::BuildRaw(0))
        {
            ioState.forwardSpeed = Fxp::BuildRaw(0);
        }
        ioState.lateralSpeed = ioState.lateralSpeed * Fxp::BuildRaw(0x00004000); // 0.25
        ioState.yawRateDegPerFrame = ioState.yawRateDegPerFrame * Fxp::BuildRaw(0x00004000); // 0.25
    }

    static void ApplyEdgeDamping(DynamicsState& ioState)
    {
        ioState.forwardSpeed = ioState.forwardSpeed * Tunables::kEdgeForwardDamping;
        ioState.lateralSpeed = ioState.lateralSpeed * Tunables::kEdgeLateralDamping;
    }

    static void Reset(DynamicsState& ioState)
    {
        ioState.forwardSpeed = Fxp::BuildRaw(0);
        ioState.lateralSpeed = Fxp::BuildRaw(0);
        ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
        ioState.steerDeg = Fxp::BuildRaw(0);
        ioState.surfaceGripScale = Tunables::kGripScaleAsphalt;
        ioState.yawAccumulatorDegRaw = 0;
        ioState.gear = 1u;
        ioState.engineRpm = Tunables::kEngineIdleRpm;
        ioState.launchStraightFrames = 0u;
        ioState.forwardLaunchLateralLockFrames = 0u;
        ioState.brakeDriftFrames = 0u;
        ioState.steerLaunchArmed = true;
        ioState.wasKinematic = false;
        ioState.wasBraking = false;
    }

private:
    static Fxp SaturateSigned(const Fxp& value, const Fxp& maxAbs)
    {
        if (value > maxAbs) return maxAbs;
        if (value < Fxp::BuildRaw(-maxAbs.RawValue())) return Fxp::BuildRaw(-maxAbs.RawValue());
        return value;
    }

    static void ApplyCoastDamping(DynamicsState& ioState)
    {
        if (ioState.forwardSpeed > Tunables::kCoastDampingPerFrame)
        {
            ioState.forwardSpeed -= Tunables::kCoastDampingPerFrame;
            return;
        }
        if (ioState.forwardSpeed < Fxp::BuildRaw(-Tunables::kCoastDampingPerFrame.RawValue()))
        {
            ioState.forwardSpeed += Tunables::kCoastDampingPerFrame;
            return;
        }
        ioState.forwardSpeed = Fxp::BuildRaw(0);
    }
};
} // namespace Game::CarPhysics
