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

        if (ioFrameState.braking)
        {
            if (ioState.forwardSpeed > Fxp::BuildRaw(0))
            {
                ioState.forwardSpeed -= Tunables::kBrakeDecelPerFrame;
            }
            else
            {
                // Brake deadzone: engage reverse only after a short hold at stop.
                if (ioFrameState.brakeHoldFrames >= Tunables::kReverseEngageDelayFrames)
                {
                    ioState.forwardSpeed -= Tunables::kReverseAccelPerFrame;
                }
                else
                {
                    ioState.forwardSpeed = Fxp::BuildRaw(0);
                }
            }
        }
        else if (throttleNorm > Fxp::BuildRaw(0))
        {
            ioState.forwardSpeed += throttleNorm * Tunables::kEngineAccelPerFrame;
            if (ioState.forwardSpeed < Fxp::BuildRaw(0))
            {
                ioState.forwardSpeed += Tunables::kBrakeDecelPerFrame;
            }
        }

        outStep.speedAbs = ioState.forwardSpeed.Abs();
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
        ioState.steerDeg += (targetSteerDeg - ioState.steerDeg) * Tunables::kSteerResponse;

        const Fxp speedRatio =
            Clamp(outStep.speedAbs / Tunables::kMaxForwardSpeed,
                  Fxp::BuildRaw(0),
                  Fxp::BuildRaw(1 << 16));
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
        steerAuthority = steerAuthority * steerGate;

        // Two-axle slip model inspired by TORCS/VDrift, adapted for fixed-point Saturn:
        // alpha_f ~= delta - (vy + lf*r)/|vx|, alpha_r ~= -(vy - lr*r)/|vx|
        // Fy = clamp(-Ca * alpha, +/- FyCap)
        // Match gameplay convention directly:
        // negative steering => left turn, positive steering => right turn.
        // Reverse steering: yaw response must be inverted when moving backwards.
        Fxp steerEffDeg = ioState.steerDeg * steerAuthority;
        if (ioState.forwardSpeed < Fxp::BuildRaw(0))
        {
            steerEffDeg = Fxp::BuildRaw(-steerEffDeg.RawValue());
        }
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
        // Body frame uses +X to the right; coupling must follow yaw sign or
        // both steering directions can drift to the same side.
        lateralAccel += yawCoupling;
        ioState.lateralSpeed += lateralAccel;
        ioState.lateralSpeed -= ioState.lateralSpeed * Tunables::kLateralDampingCoeff;

        const Fxp yawMoment =
            (fyFront * Tunables::kWheelbaseFront) - (fyRear * Tunables::kWheelbaseRear);
        const Fxp targetYawRateDegPerFrame =
            (yawMoment * Tunables::kYawMomentGain) * Tunables::kRadToDeg;

        ioState.yawRateDegPerFrame +=
            (targetYawRateDegPerFrame - ioState.yawRateDegPerFrame) * Tunables::kYawRateResponse;
        ioState.yawRateDegPerFrame -= ioState.yawRateDegPerFrame * Tunables::kYawDamping;

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
            ioState.lateralSpeed -= ioState.lateralSpeed * Tunables::kBrakeLateralDampingCoeff;
            ioState.yawRateDegPerFrame -= ioState.yawRateDegPerFrame * Tunables::kBrakeYawDampingCoeff;
            if (ioState.forwardSpeed == Fxp::BuildRaw(0) &&
                ioState.lateralSpeed.Abs() < Tunables::kBrakeResidualLateralCutoff &&
                ioState.yawRateDegPerFrame.Abs() < Tunables::kBrakeResidualYawCutoff)
            {
                ioState.lateralSpeed = Fxp::BuildRaw(0);
                ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
                ioState.yawAccumulatorDegRaw = 0;
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

        // Preserve sub-degree yaw to keep steering responsive at low speeds.
        ioState.yawAccumulatorDegRaw += ioState.yawRateDegPerFrame.RawValue();
        // Use symmetric truncation (towards zero) to avoid signed-shift bias:
        // tiny negative rates must not become -1 degree immediately.
        const int32_t yawStepDeg = ioState.yawAccumulatorDegRaw / (1 << 16);
        ioState.yawAccumulatorDegRaw -= (yawStepDeg * (1 << 16));
        ioCarYawDeg = NormalizeYaw(ioCarYawDeg + yawStepDeg);
        outStep.yawStepDeg = static_cast<int16_t>(std::clamp<int32_t>(yawStepDeg, -32768, 32767));

        // Use fractional yaw remainder for movement vector so turning does not
        // feel quantized/stuck at low speed while still keeping integer yaw state.
        const int32_t motionYawDegRaw =
            (static_cast<int32_t>(ioCarYawDeg) << 16) + ioState.yawAccumulatorDegRaw;
        const auto yawAngle =
            SRL::Math::Types::Angle::FromDegrees(Fxp::BuildRaw(motionYawDegRaw));
        outStep.sinYaw = SRL::Math::Trigonometry::Sin(yawAngle);
        outStep.cosYaw = SRL::Math::Trigonometry::Cos(yawAngle);

        const Fxp negCosYaw = Fxp::BuildRaw(-outStep.cosYaw.RawValue());
        outStep.planarDx = (outStep.sinYaw * ioState.forwardSpeed) + (outStep.cosYaw * ioState.lateralSpeed);
        outStep.planarDz = (negCosYaw * ioState.forwardSpeed) + (outStep.sinYaw * ioState.lateralSpeed);
        ioCarWorldPosition.X += outStep.planarDx;
        ioCarWorldPosition.Z += outStep.planarDz;

        ioFrameState.debugSteerDeg = FxpToDebugInt(ioState.steerDeg);
        ioFrameState.debugYawRateDeg = FxpToDebugInt(ioState.yawRateDegPerFrame);
        ioFrameState.debugYawStepDeg = static_cast<int16_t>(outStep.yawStepDeg);
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
