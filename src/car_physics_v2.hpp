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

        if constexpr (CarPhysics::Tunables::kEnableSaturnLowCostPhysics)
        {
            StepOnce(ioFrameState, trackQuery, ioCarWorldPosition, ioCarYawDeg);
        }
        else
        {
            const auto plan = scheduler_.BeginFrame();
            for (int32_t i = 0; i < plan.steps; ++i)
            {
                StepOnce(ioFrameState, trackQuery, ioCarWorldPosition, ioCarYawDeg);
            }
        }
        // Keep frame state authoritative for downstream systems.
        ioFrameState.carWorldPosition = ioCarWorldPosition;
        ioFrameState.carYawDeg = ioCarYawDeg;
    }

private:
    static CarPhysics::Fxp DotPlanar(const Vector3D& a, const Vector3D& b)
    {
        return (a.X * b.X) + (a.Z * b.Z);
    }

    static bool ProjectPlanarOntoAxis(const CarPhysics::Fxp vecX,
                                      const CarPhysics::Fxp vecZ,
                                      const CarPhysics::Fxp axisX,
                                      const CarPhysics::Fxp axisZ,
                                      CarPhysics::Fxp& outProjX,
                                      CarPhysics::Fxp& outProjZ)
    {
        const CarPhysics::Fxp axisLenSq = (axisX * axisX) + (axisZ * axisZ);
        if (axisLenSq <= CarPhysics::Tunables::kWallPushVelocityCancelThreshold)
        {
            outProjX = CarPhysics::Fxp::BuildRaw(0);
            outProjZ = CarPhysics::Fxp::BuildRaw(0);
            return false;
        }

        const int32_t axisLenSqRaw = axisLenSq.RawValue();
        if (axisLenSqRaw == 0)
        {
            outProjX = CarPhysics::Fxp::BuildRaw(0);
            outProjZ = CarPhysics::Fxp::BuildRaw(0);
            return false;
        }

        const CarPhysics::Fxp axisDot = (vecX * axisX) + (vecZ * axisZ);
        const int32_t scaleRaw = static_cast<int32_t>(
            (static_cast<int64_t>(axisDot.RawValue()) << 16) / axisLenSqRaw);
        const CarPhysics::Fxp scale = CarPhysics::Fxp::BuildRaw(scaleRaw);
        outProjX = axisX * scale;
        outProjZ = axisZ * scale;
        return true;
    }

    static bool BuildWallPlanarNormal(const CarPhysics::GroundState& groundState,
                                      CarPhysics::Fxp& outNX,
                                      CarPhysics::Fxp& outNZ,
                                      CarPhysics::Fxp& outMaxAxis)
    {
        const CarPhysics::Fxp absX = groundState.lastWallPushX.Abs();
        const CarPhysics::Fxp absZ = groundState.lastWallPushZ.Abs();
        outMaxAxis = (absX >= absZ) ? absX : absZ;
        if (outMaxAxis <= CarPhysics::Tunables::kWallPushVelocityCancelThreshold)
        {
            outNX = CarPhysics::Fxp::BuildRaw(0);
            outNZ = CarPhysics::Fxp::BuildRaw(0);
            return false;
        }

        outNX = CarPhysics::Fxp::BuildRaw(
            static_cast<int32_t>(
                (static_cast<int64_t>(groundState.lastWallPushX.RawValue()) << 16) /
                outMaxAxis.RawValue()));
        outNZ = CarPhysics::Fxp::BuildRaw(
            static_cast<int32_t>(
                (static_cast<int64_t>(groundState.lastWallPushZ.RawValue()) << 16) /
                outMaxAxis.RawValue()));
        return true;
    }

    static void ApplyLowCostWallResponse(CarPhysics::DynamicsState& ioDynamicsState,
                                         const CarPhysics::FrameStepOutput& stepOutput,
                                         const CarPhysics::GroundState& groundState,
                                         const Vector3D& preStepPosition,
                                         Vector3D& ioCarWorldPosition)
    {
        CarPhysics::Fxp normalX{};
        CarPhysics::Fxp normalZ{};
        CarPhysics::Fxp maxAxis{};
        if (!BuildWallPlanarNormal(groundState, normalX, normalZ, maxAxis))
        {
            return;
        }

        const CarPhysics::Fxp worldVelX =
            (stepOutput.sinYaw * ioDynamicsState.forwardSpeed) +
            (stepOutput.cosYaw * ioDynamicsState.lateralSpeed);
        const CarPhysics::Fxp worldVelZ =
            (CarPhysics::Fxp::BuildRaw(-stepOutput.cosYaw.RawValue()) *
             ioDynamicsState.forwardSpeed) +
            (stepOutput.sinYaw * ioDynamicsState.lateralSpeed);
        const CarPhysics::Fxp velDotNormal =
            (worldVelX * normalX) + (worldVelZ * normalZ);
        if (velDotNormal < CarPhysics::Fxp::BuildRaw(0))
        {
            CarPhysics::Fxp inwardVelX{};
            CarPhysics::Fxp inwardVelZ{};
            ProjectPlanarOntoAxis(worldVelX,
                                  worldVelZ,
                                  normalX,
                                  normalZ,
                                  inwardVelX,
                                  inwardVelZ);
            const CarPhysics::Fxp resolvedWorldVelX = worldVelX - inwardVelX;
            const CarPhysics::Fxp resolvedWorldVelZ = worldVelZ - inwardVelZ;
            ioDynamicsState.forwardSpeed =
                (resolvedWorldVelX * stepOutput.sinYaw) +
                (resolvedWorldVelZ * CarPhysics::Fxp::BuildRaw(-stepOutput.cosYaw.RawValue()));
            ioDynamicsState.lateralSpeed =
                (resolvedWorldVelX * stepOutput.cosYaw) +
                (resolvedWorldVelZ * stepOutput.sinYaw);
        }

        ioDynamicsState.forwardSpeed -=
            ioDynamicsState.forwardSpeed *
            CarPhysics::Tunables::kWallImpactForwardDamping;

        ioDynamicsState.yawRateDegPerFrame = CarPhysics::Fxp::BuildRaw(0);

        const CarPhysics::Fxp deltaX = ioCarWorldPosition.X - preStepPosition.X;
        const CarPhysics::Fxp deltaZ = ioCarWorldPosition.Z - preStepPosition.Z;
        const CarPhysics::Fxp deltaDotNormal = (deltaX * normalX) + (deltaZ * normalZ);
        if (deltaDotNormal < CarPhysics::Fxp::BuildRaw(0))
        {
            ioCarWorldPosition.X =
                ioCarWorldPosition.X - (normalX * deltaDotNormal) +
                (normalX * CarPhysics::Tunables::kWallSeparationSkin);
            ioCarWorldPosition.Z =
                ioCarWorldPosition.Z - (normalZ * deltaDotNormal) +
                (normalZ * CarPhysics::Tunables::kWallSeparationSkin);
        }

        if (ioDynamicsState.forwardSpeed.Abs() <
            CarPhysics::Tunables::kWallImpactStopCutoff)
        {
            ioDynamicsState.forwardSpeed = CarPhysics::Fxp::BuildRaw(0);
        }
        if (ioDynamicsState.lateralSpeed.Abs() <
            CarPhysics::Tunables::kWallSeparationSkin)
        {
            ioDynamicsState.lateralSpeed = CarPhysics::Fxp::BuildRaw(0);
        }
    }

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

    void ApplyBodyClipPlanarReaction(const ITrackCollisionQuery* trackQuery,
                                     GameplayFrameState& ioFrameState,
                                     const Vector3D& carWorldPosition,
                                     int32_t carYawDeg)
    {
        if constexpr (!CarPhysics::Tunables::kEnableBodyClipPlanarReaction)
        {
            return;
        }
        if (!trackQuery)
        {
            return;
        }

        const auto yawAngle = SRL::Math::Types::Angle::FromDegrees(
            SRL::Math::Types::Fxp::BuildRaw(carYawDeg << 16));
        const CarPhysics::Fxp sinYaw = SRL::Math::Trigonometry::Sin(yawAngle);
        const CarPhysics::Fxp cosYaw = SRL::Math::Trigonometry::Cos(yawAngle);
        const CarPhysics::Fxp forwardX = sinYaw;
        const CarPhysics::Fxp forwardZ = CarPhysics::Fxp::BuildRaw(-cosYaw.RawValue());
        const CarPhysics::Fxp rightX = cosYaw;
        const CarPhysics::Fxp rightZ = sinYaw;

        const Vector3D forwardDirection(forwardX, CarPhysics::Fxp::BuildRaw(0), forwardZ);
        const CarPhysics::Fxp worldVelX =
            (forwardX * dynamicsState_.forwardSpeed) + (rightX * dynamicsState_.lateralSpeed);
        const CarPhysics::Fxp worldVelZ =
            (forwardZ * dynamicsState_.forwardSpeed) + (rightZ * dynamicsState_.lateralSpeed);
        const Vector3D worldPlanarVelocity(worldVelX,
                                           CarPhysics::Fxp::BuildRaw(0),
                                           worldVelZ);

        const CarPhysics::Fxp maxPushPerClip = CarPhysics::Tunables::kBodyClipMaxPushPerClip;
        const CarPhysics::Fxp minPushPerClip =
            CarPhysics::Fxp::BuildRaw(-maxPushPerClip.RawValue());

        int32_t seedSegmentId = (groundState_.lastSurfaceSegmentId > 0)
            ? static_cast<int32_t>(groundState_.lastSurfaceSegmentId)
            : ioFrameState.activeSegmentId;
        if (seedSegmentId <= 0)
        {
            seedSegmentId = -1;
        }

        for (const auto& clip : CarPhysics::Tunables::kBodyClips)
        {
            Vector3D clipWorldPosition = carWorldPosition;
            clipWorldPosition.X +=
                (forwardX * clip.localForward) + (rightX * clip.localRight);
            clipWorldPosition.Z +=
                (forwardZ * clip.localForward) + (rightZ * clip.localRight);

            // Surface query is optional: on Saturn (kBodyClipQuerySurface=false) skip the
            // expensive per-clip probe and only run the wall push query.
            CarPhysics::SurfaceQueryResult surface{};
            bool surfaceFound = false;
            if constexpr (CarPhysics::Tunables::kBodyClipQuerySurface)
            {
                surfaceFound = CarPhysics::GroundFollower::QuerySurface(trackQuery,
                                                                         clipWorldPosition,
                                                                         seedSegmentId,
                                                                         surface);
                if (surfaceFound && surface.segmentId > 0)
                {
                    seedSegmentId = surface.segmentId;
                }
            }

            if constexpr (CarPhysics::Tunables::kEnableWallPlanarPush)
            {
                Vector3D wallPush{};
                int32_t wallSegmentId = -1;
                if (trackQuery->ResolvePlanarWallPush(clipWorldPosition,
                                                      forwardDirection,
                                                      CarPhysics::Tunables::kBodyClipWallRadius,
                                                      wallPush,
                                                      &wallSegmentId,
                                                      seedSegmentId))
                {
                    const CarPhysics::Fxp wallPushX = CarPhysics::Clamp(
                        wallPush.X * CarPhysics::Tunables::kBodyClipWallPushScale,
                        minPushPerClip,
                        maxPushPerClip);
                    const CarPhysics::Fxp wallPushZ = CarPhysics::Clamp(
                        wallPush.Z * CarPhysics::Tunables::kBodyClipWallPushScale,
                        minPushPerClip,
                        maxPushPerClip);
                    groundState_.correctionX += wallPushX;
                    groundState_.correctionZ += wallPushZ;
                    if (wallSegmentId > 0) seedSegmentId = wallSegmentId;
                    if (ioFrameState.debugWallHit == 0u)
                    {
                        ioFrameState.debugWallHit = 1u;
                        ioFrameState.debugWallSegmentId = wallSegmentId;
                        ioFrameState.debugWallPushX = CarPhysics::FxpToDebugInt(wallPushX);
                        ioFrameState.debugWallPushZ = CarPhysics::FxpToDebugInt(wallPushZ);
                    }
                }
            }

            // Penetration depth response requires surface data; skip in wall-only mode.
            if (!surfaceFound)
            {
                continue;
            }

            const CarPhysics::Fxp penetrationDepth =
                (surface.surfaceY - clipWorldPosition.Y) * surface.normal.Y;
            if (penetrationDepth <= CarPhysics::Tunables::kBodyClipPenetrationBias)
            {
                continue;
            }

            const CarPhysics::Fxp planarNormalAbs = surface.normal.X.Abs() + surface.normal.Z.Abs();
            if (planarNormalAbs < CarPhysics::Tunables::kBodyClipMinPlanarNormalAbs)
            {
                continue;
            }

            const CarPhysics::Fxp clampedDepth =
                CarPhysics::Fxp::Min(penetrationDepth, CarPhysics::Tunables::kBodyClipMaxDepth);
            const CarPhysics::Fxp response = clampedDepth * clip.force;
            const Vector3D planarNormal(surface.normal.X,
                                        CarPhysics::Fxp::BuildRaw(0),
                                        surface.normal.Z);
            const CarPhysics::Fxp normalSpeed =
                DotPlanar(worldPlanarVelocity, planarNormal);
            const CarPhysics::Fxp dampedResponse =
                response - (normalSpeed * clip.dampening);

            const CarPhysics::Fxp clipPushX = CarPhysics::Clamp(
                surface.normal.X * dampedResponse,
                minPushPerClip,
                maxPushPerClip);
            const CarPhysics::Fxp clipPushZ = CarPhysics::Clamp(
                surface.normal.Z * dampedResponse,
                minPushPerClip,
                maxPushPerClip);
            groundState_.correctionX += clipPushX;
            groundState_.correctionZ += clipPushZ;
        }

        const CarPhysics::Fxp maxPlanar = CarPhysics::Tunables::kMaxPlanarCorrectionPerFrame;
        const CarPhysics::Fxp minPlanar = CarPhysics::Fxp::BuildRaw(-maxPlanar.RawValue());
        groundState_.correctionX = CarPhysics::Clamp(groundState_.correctionX, minPlanar, maxPlanar);
        groundState_.correctionZ = CarPhysics::Clamp(groundState_.correctionZ, minPlanar, maxPlanar);
        ioFrameState.debugCorrX = CarPhysics::FxpToDebugInt(groundState_.correctionX);
        ioFrameState.debugCorrZ = CarPhysics::FxpToDebugInt(groundState_.correctionZ);
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
        CarPhysics::PublishAuthoritativeDrivetrain(ioFrameState,
                                                   static_cast<int16_t>(CarPhysics::Tunables::kNeutralGear),
                                                   CarPhysics::Tunables::kEngineIdleRpm,
                                                   0);
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
        const auto launchAssist = lateralYaw_.ApplyLaunchTurnAssist(physicsFrame, dynamicsState_);
        if (launchAssist.launchEdge)
        {
            dynamicsState_.forwardLaunchLateralLockFrames =
                CarPhysics::Tunables::kForwardLaunchLateralLockFrames;
        }

        UpdateSurfaceGripScale(trackQuery, physicsFrame, ioCarWorldPosition);

        const Vector3D preStepPosition = ioCarWorldPosition;
        const bool wasKinematicPrev = dynamicsState_.wasKinematic;
        CarPhysics::FrameStepOutput stepOutput{};
        CarPhysics::DynamicsModel::IntegratePlanar(physicsFrame,
                                                   dynamicsState_,
                                                   ioCarWorldPosition,
                                                   ioCarYawDeg,
                                                   stepOutput);
        const bool kinematicToSlipTransition =
            wasKinematicPrev && !stepOutput.wasKinematicMode;

        if (stepOutput.wasKinematicMode)
        {
            // Kinematic mode: displace along the post-step heading so the car always
            // moves in the direction it now faces — no crab-walk from pre-step yaw lag.
            const auto yawAngle = SRL::Math::Types::Angle::FromDegrees(
                SRL::Math::Types::Fxp::BuildRaw(ioCarYawDeg << 16));
            const CarPhysics::Fxp sinYaw = SRL::Math::Trigonometry::Sin(yawAngle);
            const CarPhysics::Fxp cosYaw = SRL::Math::Trigonometry::Cos(yawAngle);
            const CarPhysics::Fxp negCosYaw = CarPhysics::Fxp::BuildRaw(-cosYaw.RawValue());
            ioCarWorldPosition.X = preStepPosition.X + (sinYaw * dynamicsState_.forwardSpeed);
            ioCarWorldPosition.Z = preStepPosition.Z + (negCosYaw * dynamicsState_.forwardSpeed);
        }
        else if (kinematicToSlipTransition && launchAssist.lowSpeedAssistActive)
        {
            // First slip frame after low-speed kinematic launch:
            // drop lateral projection from the integrated displacement so entry
            // remains symmetric/deterministic for left/right starts at zero speed.
            const auto yawAngle = SRL::Math::Types::Angle::FromDegrees(
                SRL::Math::Types::Fxp::BuildRaw(ioCarYawDeg << 16));
            const CarPhysics::Fxp sinYaw = SRL::Math::Trigonometry::Sin(yawAngle);
            const CarPhysics::Fxp cosYaw = SRL::Math::Trigonometry::Cos(yawAngle);
            const CarPhysics::Fxp negCosYaw = CarPhysics::Fxp::BuildRaw(-cosYaw.RawValue());
            const CarPhysics::Fxp deltaX = ioCarWorldPosition.X - preStepPosition.X;
            const CarPhysics::Fxp deltaZ = ioCarWorldPosition.Z - preStepPosition.Z;
            CarPhysics::Fxp localLong = (sinYaw * deltaX) + (negCosYaw * deltaZ);
            if (localLong < CarPhysics::Fxp::BuildRaw(0))
            {
                localLong = CarPhysics::Fxp::BuildRaw(0);
            }
            ioCarWorldPosition.X = preStepPosition.X + (sinYaw * localLong);
            ioCarWorldPosition.Z = preStepPosition.Z + (negCosYaw * localLong);
        }

        const int32_t sampledSegmentId =
            CarPhysics::GroundFollower::UpdateTarget(trackQuery,
                                                     ioCarWorldPosition,
                                                     stepOutput,
                                                     groundState_,
                                                     physicsFrame);
        if constexpr (CarPhysics::Tunables::kEnableBodyClipPlanarReaction)
        {
            ApplyBodyClipPlanarReaction(trackQuery,
                                        physicsFrame,
                                        ioCarWorldPosition,
                                        ioCarYawDeg);
        }
        CarPhysics::GroundFollower::ApplyVerticalAdhesion(groundState_, ioCarWorldPosition);

        if (trackQuery && CarPhysics::Tunables::kEnableWallPlanarPush &&
            groundState_.lastWallQueryHit)
        {
            if constexpr (CarPhysics::Tunables::kEnableSaturnLowCostPhysics)
            {
                ApplyLowCostWallResponse(dynamicsState_,
                                         stepOutput,
                                         groundState_,
                                         preStepPosition,
                                         ioCarWorldPosition);
            }
            else
            {
                const CarPhysics::Fxp absX = groundState_.lastWallPushX.Abs();
                const CarPhysics::Fxp absZ = groundState_.lastWallPushZ.Abs();
                const CarPhysics::Fxp maxAxis = (absX >= absZ) ? absX : absZ;
                // Cancel velocity directed into the wall so the car doesn't re-tunnel next frame.
                // Uses lastWallPushX/Z from the current frame's ground follower query (~10 muls).
                if (maxAxis > CarPhysics::Tunables::kWallPushVelocityCancelThreshold)
                {
                    const CarPhysics::Fxp nX = CarPhysics::Fxp::BuildRaw(
                        static_cast<int32_t>(
                            (static_cast<int64_t>(groundState_.lastWallPushX.RawValue()) << 16) /
                            maxAxis.RawValue()));
                    const CarPhysics::Fxp nZ = CarPhysics::Fxp::BuildRaw(
                        static_cast<int32_t>(
                            (static_cast<int64_t>(groundState_.lastWallPushZ.RawValue()) << 16) /
                            maxAxis.RawValue()));
                    const CarPhysics::Fxp worldVelX =
                        (stepOutput.sinYaw * dynamicsState_.forwardSpeed) +
                        (stepOutput.cosYaw * dynamicsState_.lateralSpeed);
                    const CarPhysics::Fxp worldVelZ =
                        (CarPhysics::Fxp::BuildRaw(-stepOutput.cosYaw.RawValue()) *
                         dynamicsState_.forwardSpeed) +
                        (stepOutput.sinYaw * dynamicsState_.lateralSpeed);
                    const CarPhysics::Fxp velDotNormal =
                        (worldVelX * nX) + (worldVelZ * nZ);
                    if (velDotNormal < CarPhysics::Fxp::BuildRaw(0))
                    {
                        const CarPhysics::Fxp cancelX =
                            CarPhysics::Fxp::BuildRaw(-((nX * velDotNormal).RawValue()));
                        const CarPhysics::Fxp cancelZ =
                            CarPhysics::Fxp::BuildRaw(-((nZ * velDotNormal).RawValue()));
                        dynamicsState_.forwardSpeed +=
                            (cancelX * stepOutput.sinYaw) +
                            (cancelZ * CarPhysics::Fxp::BuildRaw(-stepOutput.cosYaw.RawValue()));
                        dynamicsState_.lateralSpeed +=
                            (cancelX * stepOutput.cosYaw) +
                            (cancelZ * stepOutput.sinYaw);
                    }
                }

                // Hard positional anti-press-through: remove only the displacement component
                // directed into the wall normal, keeping tangential slide.
                if (maxAxis > CarPhysics::Tunables::kWallPushVelocityCancelThreshold)
                {
                    const CarPhysics::Fxp nX = CarPhysics::Fxp::BuildRaw(
                        static_cast<int32_t>(
                            (static_cast<int64_t>(groundState_.lastWallPushX.RawValue()) << 16) /
                            maxAxis.RawValue()));
                    const CarPhysics::Fxp nZ = CarPhysics::Fxp::BuildRaw(
                        static_cast<int32_t>(
                            (static_cast<int64_t>(groundState_.lastWallPushZ.RawValue()) << 16) /
                            maxAxis.RawValue()));
                    const CarPhysics::Fxp deltaX = ioCarWorldPosition.X - preStepPosition.X;
                    const CarPhysics::Fxp deltaZ = ioCarWorldPosition.Z - preStepPosition.Z;
                    const CarPhysics::Fxp deltaDotNormal = (deltaX * nX) + (deltaZ * nZ);
                    if (deltaDotNormal < CarPhysics::Fxp::BuildRaw(0))
                    {
                        ioCarWorldPosition.X =
                            ioCarWorldPosition.X - (nX * deltaDotNormal);
                        ioCarWorldPosition.Z =
                            ioCarWorldPosition.Z - (nZ * deltaDotNormal);
                    }
                }
            }
        }

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
        ioFrameState.carGear = physicsFrame.carGear;
        ioFrameState.carEngineRpm = physicsFrame.carEngineRpm;
        ioFrameState.carSpeedKmh = physicsFrame.carSpeedKmh;
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
