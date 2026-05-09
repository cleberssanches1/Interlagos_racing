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

        ioState.forwardSpeed += throttleNorm * Tunables::kEngineAccelPerFrame;
        if (ioFrameState.braking)
        {
            ioState.forwardSpeed -= Tunables::kBrakeDecelPerFrame;
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
                                     Fxp::BuildRaw(0),
                                     Tunables::kMaxForwardSpeed);

        const Fxp targetSteerDeg =
            Fxp::BuildRaw(-steerNorm.RawValue()) * Tunables::kMaxSteerDeg;
        ioState.steerDeg += (targetSteerDeg - ioState.steerDeg) * Tunables::kSteerResponse;

        const Fxp speedRatio =
            Clamp(outStep.speedAbs / Tunables::kMaxForwardSpeed,
                  Fxp::BuildRaw(0),
                  Fxp::BuildRaw(1 << 16));
        const Fxp steerGrip =
            Fxp::BuildRaw(1 << 16) - (speedRatio * Tunables::kHighSpeedSteerLoss);

        ioState.yawRateDegPerFrame =
            ioState.steerDeg * (ioState.forwardSpeed * Tunables::kYawFromSpeedCoeff) * steerGrip;
        ioState.yawRateDegPerFrame -= ioState.yawRateDegPerFrame * Tunables::kYawDamping;
        ioCarYawDeg = NormalizeYaw(ioCarYawDeg + ioState.yawRateDegPerFrame.As<int16_t>());

        const auto yawAngle =
            SRL::Math::Types::Angle::FromDegrees(
                Fxp::BuildRaw(static_cast<int32_t>(ioCarYawDeg) << 16));
        outStep.sinYaw = SRL::Math::Trigonometry::Sin(yawAngle);
        outStep.cosYaw = SRL::Math::Trigonometry::Cos(yawAngle);

        ioState.lateralSpeed += ioState.steerDeg * ioState.forwardSpeed * Tunables::kLateralCouplingCoeff;
        ioState.lateralSpeed -= ioState.lateralSpeed * Tunables::kLateralDampingCoeff;

        const Fxp negCosYaw = Fxp::BuildRaw(-outStep.cosYaw.RawValue());
        ioCarWorldPosition.X += (outStep.sinYaw * ioState.forwardSpeed) + (outStep.cosYaw * ioState.lateralSpeed);
        ioCarWorldPosition.Z += (negCosYaw * ioState.forwardSpeed) + (outStep.sinYaw * ioState.lateralSpeed);
    }

    static void Reset(DynamicsState& ioState)
    {
        ioState.forwardSpeed = Fxp::BuildRaw(0);
        ioState.lateralSpeed = Fxp::BuildRaw(0);
        ioState.yawRateDegPerFrame = Fxp::BuildRaw(0);
        ioState.steerDeg = Fxp::BuildRaw(0);
    }

private:
    static void ApplyCoastDamping(DynamicsState& ioState)
    {
        if (ioState.forwardSpeed > Tunables::kCoastDampingPerFrame)
        {
            ioState.forwardSpeed -= Tunables::kCoastDampingPerFrame;
            return;
        }
        ioState.forwardSpeed = Fxp::BuildRaw(0);
    }
};
} // namespace Game::CarPhysics

