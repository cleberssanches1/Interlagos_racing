#pragma once

#include "car_physics_shared.hpp"
#include "interfaces.hpp"

namespace Game::CarPhysicsV2
{
class LateralYawController
{
public:
    struct LaunchAssistResult
    {
        bool lowSpeedAssistActive = false;
        bool launchEdge = false;
    };

    void Reset()
    {
        launchCommandPrev_ = false;
        prevSteerSign_ = 0;
        lastAssistResult_ = LaunchAssistResult{};
    }

    LaunchAssistResult ApplyLaunchTurnAssist(const GameplayFrameState& frame,
                                             CarPhysics::DynamicsState& state)
    {
        lastAssistResult_ = LaunchAssistResult{};
        const bool throttleOn = frame.throttle > 0;
        const bool brakeOn = frame.braking;
        const bool hasSteer = (frame.steering != 0);
        if (!throttleOn || brakeOn || !hasSteer)
        {
            launchCommandPrev_ = false;
            prevSteerSign_ = 0;
            return lastAssistResult_;
        }

        const CarPhysics::Fxp vxAbs = state.forwardSpeed.Abs();
        const CarPhysics::Fxp launchSpeedLimit =
            CarPhysics::Tunables::kForwardSteerSignLockSpeedThreshold;
        if (vxAbs >= launchSpeedLimit || state.forwardSpeed < CarPhysics::Fxp::BuildRaw(0))
        {
            launchCommandPrev_ = true;
            prevSteerSign_ = (frame.steering < 0) ? int8_t(-1) : int8_t(1);
            return lastAssistResult_;
        }

        const int8_t steerSign = (frame.steering < 0) ? int8_t(-1) : int8_t(1);
        lastAssistResult_.lowSpeedAssistActive = true;
        const bool steerDirectionChanged =
            (prevSteerSign_ != 0) && (steerSign != prevSteerSign_);

        const bool launchCommandRisingEdge =
            (!launchCommandPrev_ || steerDirectionChanged);
        launchCommandPrev_ = true;
        prevSteerSign_ = steerSign;

        if (launchCommandRisingEdge)
        {
            // Start a new low-speed arc from a clean state to keep
            // left/right launch response symmetric.
            state.lateralSpeed = CarPhysics::Fxp::BuildRaw(0);
            state.yawRateDegPerFrame = CarPhysics::Fxp::BuildRaw(0);
            state.yawAccumulatorDegRaw = 0;
            lastAssistResult_.launchEdge = true;
        }
        return lastAssistResult_;
    }

private:
    bool launchCommandPrev_ = false;
    int8_t prevSteerSign_ = 0;
    LaunchAssistResult lastAssistResult_{};
};
} // namespace Game::CarPhysicsV2
