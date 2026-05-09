#pragma once

#include "car_dynamics_model.hpp"
#include "car_ground_follower.hpp"

namespace Game
{
// Orchestrates separated subsystems:
// - planar dynamics (X/Z + yaw)
// - ground probing and vertical adhesion (Y)
class SimpleCarPhysics final : public ICarPhysics
{
public:
    void Step(GameplayFrameState& ioFrameState,
              const ITrackCollisionQuery* trackQuery,
              Vector3D& ioCarWorldPosition,
              int32_t& ioCarYawDeg) override
    {
        CarPhysics::ResetGroundDebug(ioFrameState);

        if (ioFrameState.resetRequested)
        {
            ResetState(ioFrameState);
            return;
        }

        CarPhysics::FrameStepOutput stepOutput{};
        CarPhysics::DynamicsModel::IntegratePlanar(ioFrameState,
                                                   dynamicsState_,
                                                   ioCarWorldPosition,
                                                   ioCarYawDeg,
                                                   stepOutput);

        const int32_t sampledSegmentId =
            CarPhysics::GroundFollower::UpdateTarget(trackQuery,
                                                     ioCarWorldPosition,
                                                     stepOutput,
                                                     groundState_,
                                                     ioFrameState);
        CarPhysics::GroundFollower::ApplyVerticalAdhesion(groundState_, ioCarWorldPosition);

        ioFrameState.activeSegmentId = sampledSegmentId;
        ioFrameState.speedProxy = CarPhysics::BuildSpeedProxy(dynamicsState_.forwardSpeed);
    }

private:
    void ResetState(GameplayFrameState& ioFrameState)
    {
        CarPhysics::DynamicsModel::Reset(dynamicsState_);
        CarPhysics::GroundFollower::Reset(groundState_);
        ioFrameState.speedProxy = 0;
        CarPhysics::ResetGroundDebug(ioFrameState);
    }

    CarPhysics::DynamicsState dynamicsState_{};
    CarPhysics::GroundState groundState_{};
};
} // namespace Game

