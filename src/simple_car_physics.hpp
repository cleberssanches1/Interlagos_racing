#pragma once

#include <algorithm>
#include <cstdlib>

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

        const int32_t gripSeedSegmentId = (groundState_.lastSurfaceSegmentId > 0)
            ? static_cast<int32_t>(groundState_.lastSurfaceSegmentId)
            : ioFrameState.activeSegmentId;
        dynamicsState_.surfaceGripScale =
            CarPhysics::GroundFollower::ResolveSurfaceGripScale(trackQuery,
                                                                ioCarWorldPosition,
                                                                gripSeedSegmentId);

        const Vector3D preStepPosition = ioCarWorldPosition;
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

        const int32_t plannedDxRaw = stepOutput.planarDx.RawValue();
        const int32_t plannedDzRaw = stepOutput.planarDz.RawValue();
        const int32_t netDxRaw =
            ioCarWorldPosition.X.RawValue() - preStepPosition.X.RawValue();
        const int32_t netDzRaw =
            ioCarWorldPosition.Z.RawValue() - preStepPosition.Z.RawValue();
        ioFrameState.debugNetDx =
            static_cast<int16_t>(std::clamp<int32_t>(netDxRaw >> 16, -32768, 32767));
        ioFrameState.debugNetDz =
            static_cast<int16_t>(std::clamp<int32_t>(netDzRaw >> 16, -32768, 32767));
        (void)plannedDxRaw;
        (void)plannedDzRaw;

        if (!groundState_.hasGroundSupport)
        {
            CarPhysics::DynamicsModel::ApplyNoSupportRecovery(dynamicsState_);
            if (groundState_.lastStablePlanarInitialized)
            {
                ioCarWorldPosition.X = groundState_.lastStableX;
                ioCarWorldPosition.Z = groundState_.lastStableZ;
            }
            else
            {
                ioCarWorldPosition.X = preStepPosition.X;
                ioCarWorldPosition.Z = preStepPosition.Z;
            }
        }
        else if (groundState_.edgeLeftLost || groundState_.edgeRightLost)
        {
            CarPhysics::DynamicsModel::ApplyEdgeDamping(dynamicsState_);
        }

        // Safety guard: keep car within renderable world limits.
        constexpr int32_t kRawLimit = (32767 << 16);
        const int32_t xRaw = ioCarWorldPosition.X.RawValue();
        const int32_t yRaw = ioCarWorldPosition.Y.RawValue();
        const int32_t zRaw = ioCarWorldPosition.Z.RawValue();
        const bool invalidPos =
            (xRaw < -kRawLimit || xRaw > kRawLimit) ||
            (yRaw < -kRawLimit || yRaw > kRawLimit) ||
            (zRaw < -kRawLimit || zRaw > kRawLimit);
        if (invalidPos)
        {
            ioCarWorldPosition = preStepPosition;
            CarPhysics::DynamicsModel::Reset(dynamicsState_);
        }

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
