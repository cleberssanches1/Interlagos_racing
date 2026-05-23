#pragma once

#include <algorithm>
#include <cstdlib>

#include "car_dynamics_model.hpp"
#include "car_ground_follower.hpp"
#include "car_physics_v2_fixed_step.hpp"
#include "car_physics_v2_lateral_yaw.hpp"
#include "car_physics_v2_longitudinal.hpp"

namespace Game
{
// Parallel V2 orchestration layer (feature-flagged).
// Initial rollout intentionally mirrors SimpleCarPhysics behavior while
// introducing fixed-step scheduling scaffolding.
class VehiclePhysicsV2 final : public ICarPhysics
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

        const auto plan = scheduler_.BeginFrame();
        for (int32_t i = 0; i < plan.steps; ++i)
        {
            StepOnce(ioFrameState, trackQuery, ioCarWorldPosition, ioCarYawDeg);
        }
        // Keep frame state authoritative for downstream systems.
        ioFrameState.carWorldPosition = ioCarWorldPosition;
        ioFrameState.carYawDeg = ioCarYawDeg;
    }

private:
    static SRL::Math::Types::Fxp ResolveGripScaleFromSurfaceType(uint8_t surfaceType)
    {
        switch (surfaceType)
        {
        case CarPhysics::Tunables::kSurfaceTypeAsphalt:
            return CarPhysics::Tunables::kGripScaleAsphalt;
        case CarPhysics::Tunables::kSurfaceTypeEscapeArea:
        case CarPhysics::Tunables::kSurfaceTypeGrass:
            return CarPhysics::Tunables::kGripScaleOffroad;
        default:
            break;
        }
        return CarPhysics::Tunables::kGripScaleFallback;
    }

    void UpdateSurfaceGripScale(const ITrackCollisionQuery* trackQuery,
                                GameplayFrameState& frameState,
                                const Vector3D& worldPosition)
    {
        const int32_t gripSeedSegmentId = (groundState_.lastSurfaceSegmentId > 0)
            ? static_cast<int32_t>(groundState_.lastSurfaceSegmentId)
            : frameState.activeSegmentId;
        if (groundState_.lastSurfaceType != 0u)
        {
            dynamicsState_.surfaceGripScale =
                ResolveGripScaleFromSurfaceType(groundState_.lastSurfaceType);
            frameState.groundFaceIndex = groundState_.lastSurfaceFaceIndex;
            frameState.groundFamilyId = groundState_.lastSurfaceFamilyId;
            frameState.groundSurfaceType = groundState_.lastSurfaceType;
            return;
        }

        if constexpr (!CarPhysics::Tunables::kEnableFaceCache)
        {
            dynamicsState_.surfaceGripScale =
                CarPhysics::GroundFollower::ResolveSurfaceGripScale(trackQuery,
                                                                    worldPosition,
                                                                    gripSeedSegmentId);
            return;
        }

        const bool segmentChanged = gripSeedSegmentId != lastGripSeedSegmentId_;
        const bool mustResampleGrip =
            segmentChanged || (groundState_.auxProbeCooldown == 0u);
        if (mustResampleGrip)
        {
            dynamicsState_.surfaceGripScale =
                CarPhysics::GroundFollower::ResolveSurfaceGripScale(trackQuery,
                                                                    worldPosition,
                                                                    gripSeedSegmentId);
            lastGripSeedSegmentId_ = gripSeedSegmentId;
            groundState_.auxProbeCooldown = CarPhysics::Tunables::kGripProbeIntervalFrames;
        }
        if (groundState_.auxProbeCooldown > 0u)
        {
            --groundState_.auxProbeCooldown;
        }
    }

    void ResetState(GameplayFrameState& ioFrameState)
    {
        CarPhysics::DynamicsModel::Reset(dynamicsState_);
        CarPhysics::GroundFollower::Reset(groundState_);
        longitudinal_.Reset();
        lateralYaw_.Reset();
        scheduler_.Reset();
        lastGripSeedSegmentId_ = -1;
        ioFrameState.speedProxy = 0;
        CarPhysics::ResetGroundDebug(ioFrameState);
    }

    void StepOnce(GameplayFrameState& ioFrameState,
                  const ITrackCollisionQuery* trackQuery,
                  Vector3D& ioCarWorldPosition,
                  int32_t& ioCarYawDeg)
    {
        GameplayFrameState physicsFrame = ioFrameState;
        physicsFrame.carWorldPosition = ioCarWorldPosition;
        physicsFrame.carYawDeg = ioCarYawDeg;
        longitudinal_.PrepareInputs(ioFrameState, dynamicsState_, physicsFrame);
        lateralYaw_.ApplyLaunchTurnAssist(physicsFrame, dynamicsState_);

        UpdateSurfaceGripScale(trackQuery, physicsFrame, ioCarWorldPosition);

        const Vector3D preStepPosition = ioCarWorldPosition;
        const int32_t preStepYawDeg = ioCarYawDeg;
        CarPhysics::FrameStepOutput stepOutput{};
        CarPhysics::DynamicsModel::IntegratePlanar(physicsFrame,
                                                   dynamicsState_,
                                                   ioCarWorldPosition,
                                                   ioCarYawDeg,
                                                   stepOutput);

        if (lateralYaw_.ShouldForceStraightThisFrame(physicsFrame.frameId))
        {
            // Lock launch displacement to forward axis for this frame.
            const auto yawAngle = SRL::Math::Types::Angle::FromDegrees(
                SRL::Math::Types::Fxp::BuildRaw(preStepYawDeg << 16));
            const CarPhysics::Fxp sinYaw = SRL::Math::Trigonometry::Sin(yawAngle);
            const CarPhysics::Fxp cosYaw = SRL::Math::Trigonometry::Cos(yawAngle);
            const CarPhysics::Fxp negCosYaw = CarPhysics::Fxp::BuildRaw(-cosYaw.RawValue());

            const Vector3D delta = ioCarWorldPosition - preStepPosition;
            const CarPhysics::Fxp localLong =
                (sinYaw * delta.X) + (negCosYaw * delta.Z);

            ioCarWorldPosition.X = preStepPosition.X + (sinYaw * localLong);
            ioCarWorldPosition.Z = preStepPosition.Z + (negCosYaw * localLong);
            ioCarYawDeg = preStepYawDeg;
        }

        const int32_t sampledSegmentId =
            CarPhysics::GroundFollower::UpdateTarget(trackQuery,
                                                     ioCarWorldPosition,
                                                     stepOutput,
                                                     groundState_,
                                                     physicsFrame);
        CarPhysics::GroundFollower::ApplyVerticalAdhesion(groundState_, ioCarWorldPosition);

        const int32_t netDxRaw =
            ioCarWorldPosition.X.RawValue() - preStepPosition.X.RawValue();
        const int32_t netDzRaw =
            ioCarWorldPosition.Z.RawValue() - preStepPosition.Z.RawValue();
        physicsFrame.debugNetDx =
            static_cast<int16_t>(std::clamp<int32_t>(netDxRaw >> 16, -32768, 32767));
        physicsFrame.debugNetDz =
            static_cast<int16_t>(std::clamp<int32_t>(netDzRaw >> 16, -32768, 32767));

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

        physicsFrame.activeSegmentId = sampledSegmentId;
        physicsFrame.speedProxy = CarPhysics::BuildSpeedProxy(dynamicsState_.forwardSpeed);

        // Copy simulation outputs while preserving raw command inputs.
        ioFrameState.carWorldPosition = ioCarWorldPosition;
        ioFrameState.carYawDeg = ioCarYawDeg;
        ioFrameState.steering = physicsFrame.steering;
        ioFrameState.throttle = physicsFrame.throttle;
        ioFrameState.braking = physicsFrame.braking;
        ioFrameState.speedProxy = physicsFrame.speedProxy;
        ioFrameState.activeSegmentId = physicsFrame.activeSegmentId;
        ioFrameState.debugGroundYRear = physicsFrame.debugGroundYRear;
        ioFrameState.debugGroundYFront = physicsFrame.debugGroundYFront;
        ioFrameState.debugGroundYTarget = physicsFrame.debugGroundYTarget;
        ioFrameState.debugGroundYRearRaw = physicsFrame.debugGroundYRearRaw;
        ioFrameState.debugGroundYFrontRaw = physicsFrame.debugGroundYFrontRaw;
        ioFrameState.debugGroundMask = physicsFrame.debugGroundMask;
        ioFrameState.debugSteerDeg = physicsFrame.debugSteerDeg;
        ioFrameState.debugYawRateDeg = physicsFrame.debugYawRateDeg;
        ioFrameState.debugYawStepDeg = physicsFrame.debugYawStepDeg;
        ioFrameState.debugEngineRpm = physicsFrame.debugEngineRpm;
        ioFrameState.debugGear = physicsFrame.debugGear;
        ioFrameState.debugSpeedKmh = physicsFrame.debugSpeedKmh;
        ioFrameState.debugPlanarDx = physicsFrame.debugPlanarDx;
        ioFrameState.debugPlanarDz = physicsFrame.debugPlanarDz;
        ioFrameState.debugNetDx = physicsFrame.debugNetDx;
        ioFrameState.debugNetDz = physicsFrame.debugNetDz;
        ioFrameState.debugCorrX = physicsFrame.debugCorrX;
        ioFrameState.debugCorrZ = physicsFrame.debugCorrZ;
        ioFrameState.debugWallHit = physicsFrame.debugWallHit;
        ioFrameState.debugWallPushX = physicsFrame.debugWallPushX;
        ioFrameState.debugWallPushZ = physicsFrame.debugWallPushZ;
        ioFrameState.debugWallSegmentId = physicsFrame.debugWallSegmentId;
        ioFrameState.groundFaceIndex = physicsFrame.groundFaceIndex;
        ioFrameState.groundFamilyId = physicsFrame.groundFamilyId;
        ioFrameState.groundSurfaceType = physicsFrame.groundSurfaceType;
    }

    CarPhysics::DynamicsState dynamicsState_{};
    CarPhysics::GroundState groundState_{};
    CarPhysicsV2::FixedStepScheduler scheduler_{};
    CarPhysicsV2::LongitudinalController longitudinal_{};
    CarPhysicsV2::LateralYawController lateralYaw_{};
    int32_t lastGripSeedSegmentId_ = -1;
};
} // namespace Game
