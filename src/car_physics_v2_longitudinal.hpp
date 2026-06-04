#pragma once

#include "car_physics_shared.hpp"
#include "interfaces.hpp"

namespace Game::CarPhysicsV2
{
class LongitudinalController
{
public:
    enum class Mode : uint8_t
    {
        Idle = 0,
        DriveForward = 1,
        BrakeToStop = 2,
        ReverseEngage = 3,
        DriveReverse = 4
    };

    void Reset()
    {
        mode_ = Mode::Idle;
        transitionHoldFrames_ = 0u;
    }

    void PrepareInputs(const GameplayFrameState& rawFrame,
                       CarPhysics::DynamicsState& dynamics,
                       GameplayFrameState& outFrame)
    {
        PrepareInputsCore(rawFrame, dynamics, outFrame);
        if ((rawFrame.throttle == 0) &&
            !rawFrame.braking &&
            (dynamics.forwardSpeed > CarPhysics::Fxp::BuildRaw(0)))
        {
            ApplyCoastLinearDrag(dynamics);
        }
    }

    void PrepareInputs(const GameplayFrameState& rawFrame,
                       const CarPhysics::DynamicsState& dynamics,
                       GameplayFrameState& outFrame)
    {
        PrepareInputsCore(rawFrame, dynamics, outFrame);
    }

    Mode CurrentMode() const { return mode_; }

private:
    void PrepareInputsCore(const GameplayFrameState& rawFrame,
                           const CarPhysics::DynamicsState& dynamics,
                           GameplayFrameState& outFrame)
    {
        outFrame = rawFrame;

        const bool throttleOn = rawFrame.throttle > 0;
        const bool brakeOn = rawFrame.braking;
        const bool movingForward = dynamics.forwardSpeed > CarPhysics::Fxp::BuildRaw(0);

        if (brakeOn)
        {
            outFrame.throttle = 0;
            outFrame.braking = true;
            (void)movingForward;
            mode_ = Mode::BrakeToStop;
        }
        else if (throttleOn)
        {
            outFrame.braking = false;
            // Launch with steering: mirror reverse's gentle pickup so the chassis
            // does not receive a large lateral impulse from full first-gear torque.
            const bool hasSteer = (rawFrame.steering != 0);
            const bool forwardOrStopped = dynamics.forwardSpeed >= CarPhysics::Fxp::BuildRaw(0);
            const bool launchSteerForward =
                hasSteer &&
                forwardOrStopped &&
                (dynamics.forwardSpeed.Abs() < CarPhysics::Tunables::kForwardSteerLaunchSpeedThreshold);
            if (launchSteerForward)
            {
                const int32_t rawThrottle =
                    (CarPhysics::Tunables::kForwardSteerLaunchAccelPerFrame.RawValue() * 100) /
                    std::max<int32_t>(1, CarPhysics::Tunables::GearAccelFor(1u).RawValue());
                const int16_t throttleCap = static_cast<int16_t>(std::clamp<int32_t>(rawThrottle, 15, 100));
                if (outFrame.throttle > throttleCap) outFrame.throttle = throttleCap;
            }
            // Conservative pass-through for drive input during rollout:
            // avoid accidentally suppressing throttle and freezing movement.
            transitionHoldFrames_ = 0u;
            mode_ = Mode::DriveForward;
        }
        else
        {
            outFrame.throttle = 0;
            outFrame.braking = false;
            transitionHoldFrames_ = 0u;
            mode_ = Mode::Idle;
        }
    }

    static void ApplyCoastLinearDrag(CarPhysics::DynamicsState& dynamics)
    {
        // Trigger Rally-inspired linear drag term (F = -k*v) for natural coasting.
        dynamics.forwardSpeed -= dynamics.forwardSpeed * CarPhysics::Tunables::kRollingDragCoeff;
    }

    Mode mode_ = Mode::Idle;
    uint8_t transitionHoldFrames_ = 0u;
};
} // namespace Game::CarPhysicsV2
