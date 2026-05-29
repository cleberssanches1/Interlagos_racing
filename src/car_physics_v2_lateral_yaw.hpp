#pragma once

#include "car_physics_shared.hpp"
#include "interfaces.hpp"

namespace Game::CarPhysicsV2
{
class LateralYawController
{
public:
    enum class Phase : uint8_t
    {
        None = 0,
        Lock = 1,
        Blend = 2
    };

    void Reset()
    {
        launchCommandPrev_ = false;
        phase_ = Phase::None;
        phaseStartFrameId_ = 0u;
    }

    void ApplyLaunchTurnAssist(const GameplayFrameState& frame,
                               CarPhysics::DynamicsState& state)
    {
        const bool throttleOn = frame.throttle > 0;
        const bool brakeOn = frame.braking;
        const bool hasSteer = (frame.steering != 0);
        if (!throttleOn || brakeOn || !hasSteer)
        {
            launchCommandPrev_ = false;
            phase_ = Phase::None;
            return;
        }

        const CarPhysics::Fxp vxAbs = state.forwardSpeed.Abs();
        const CarPhysics::Fxp launchSpeedLimit = CarPhysics::Fxp::BuildRaw(0x00018000); // 1.5
        if (vxAbs >= launchSpeedLimit || state.forwardSpeed < CarPhysics::Fxp::BuildRaw(0))
        {
            launchCommandPrev_ = true;
            phase_ = Phase::None;
            return;
        }

        const bool launchCommandNow = true;
        const bool launchCommandRisingEdge = (!launchCommandPrev_) && launchCommandNow;
        launchCommandPrev_ = launchCommandNow;

        if (launchCommandRisingEdge)
        {
            phase_ = Phase::Lock;
            phaseStartFrameId_ = frame.frameId;
            state.yawAccumulatorDegRaw = 0;
        }

        if (phase_ == Phase::Lock)
        {
            const uint32_t lockDelta = frame.frameId - phaseStartFrameId_;
            if (lockDelta < kLockFrames)
            {
                state.lateralSpeed = CarPhysics::Fxp::BuildRaw(0);
                state.yawRateDegPerFrame = CarPhysics::Fxp::BuildRaw(0);
                state.yawAccumulatorDegRaw = 0;
                return;
            }
            phase_ = Phase::Blend;
            phaseStartFrameId_ = frame.frameId;
        }

        // Remove side-slip impulse and enter arc.
        state.lateralSpeed = CarPhysics::Fxp::BuildRaw(0);

        // Force minimum yaw response proportional to steering magnitude.
        const int16_t steerAbs =
            static_cast<int16_t>((frame.steering < 0) ? -frame.steering : frame.steering);
        const CarPhysics::Fxp steerNorm =
            CarPhysics::Fxp::BuildRaw((static_cast<int32_t>(steerAbs) << 16) / 100);
        CarPhysics::Fxp minYaw = steerNorm * CarPhysics::Fxp::BuildRaw(0x0000A000); // ~0.625 deg/f
        if (phase_ == Phase::Blend)
        {
            const uint32_t blendDelta = frame.frameId - phaseStartFrameId_;
            const CarPhysics::Fxp alpha = BlendAlpha(blendDelta);
            minYaw = minYaw * alpha;
            if (blendDelta >= kBlendFrames)
            {
                phase_ = Phase::None;
            }
        }
        if (frame.steering < 0)
        {
            if (state.yawRateDegPerFrame > CarPhysics::Fxp::BuildRaw(-minYaw.RawValue()))
            {
                state.yawRateDegPerFrame = CarPhysics::Fxp::BuildRaw(-minYaw.RawValue());
            }
        }
        else
        {
            if (state.yawRateDegPerFrame < minYaw)
            {
                state.yawRateDegPerFrame = minYaw;
            }
        }
    }

    bool ShouldForceStraightThisFrame(uint32_t frameId) const
    {
        if (phase_ != Phase::Lock) return false;
        const uint32_t lockDelta = frameId - phaseStartFrameId_;
        return lockDelta < kLockFrames;
    }

private:
    static constexpr uint32_t kLockFrames = 0u;
    static constexpr uint32_t kBlendFrames = 4u;

    static CarPhysics::Fxp BlendAlpha(uint32_t blendDelta)
    {
        switch (blendDelta)
        {
        case 0u: return CarPhysics::Fxp::BuildRaw(0x00004000); // 0.25
        case 1u: return CarPhysics::Fxp::BuildRaw(0x00008000); // 0.5
        case 2u: return CarPhysics::Fxp::BuildRaw(0x0000C000); // 0.75
        default: return CarPhysics::Fxp::BuildRaw(0x00010000); // 1.0
        }
    }

    bool launchCommandPrev_ = false;
    Phase phase_ = Phase::None;
    uint32_t phaseStartFrameId_ = 0u;
};
} // namespace Game::CarPhysicsV2
