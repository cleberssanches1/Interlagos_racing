#pragma once

#include <utility>

#include "car_physics_shared.hpp"

namespace Game::CarPhysics
{
class GroundFollower
{
public:
    static Fxp ResolveSurfaceGripScale(const ITrackCollisionQuery* trackQuery,
                                       const Vector3D& worldPosition,
                                       int32_t seedSegmentId)
    {
        if (!trackQuery)
        {
            return Tunables::kGripScaleFallback;
        }

        Vector3D samplePosition = worldPosition;
        samplePosition.Y -= Tunables::kSurfaceSampleDownBias;

        Fxp asphaltY{};
        int32_t asphaltSegmentId = -1;
        bool asphaltFound = false;
        if constexpr (Tunables::kEnableSurfaceTypeQuery)
        {
            asphaltFound = trackQuery->SampleSurfaceYBySurfaceTypeSet(
                samplePosition,
                Tunables::kAsphaltSurfaceTypes.data(),
                Tunables::kAsphaltSurfaceTypes.size(),
                asphaltY,
                &asphaltSegmentId,
                seedSegmentId);
        }
        else
        {
            asphaltFound = trackQuery->SampleSurfaceYByFamilyId(
                samplePosition,
                Tunables::kAsphaltFamilyId,
                asphaltY,
                &asphaltSegmentId,
                seedSegmentId);
        }
        if (asphaltFound)
        {
            const Fxp deltaY = (asphaltY - samplePosition.Y).Abs();
            if (deltaY < Fxp::BuildRaw(0x0000C000)) // 0.75
            {
                return Tunables::kGripScaleAsphalt;
            }
        }

        Fxp driveableY{};
        int32_t driveableSegmentId = -1;
        bool driveableFound = false;
        if constexpr (Tunables::kEnableSurfaceTypeQuery)
        {
            driveableFound = trackQuery->SampleSurfaceYBySurfaceTypeSet(
                samplePosition,
                Tunables::kDriveableSurfaceTypes.data(),
                Tunables::kDriveableSurfaceTypes.size(),
                driveableY,
                &driveableSegmentId,
                seedSegmentId);
        }
        else
        {
            driveableFound = trackQuery->SampleSurfaceYByFamilySet(
                samplePosition,
                Tunables::kDriveableFamilies.data(),
                Tunables::kDriveableFamilies.size(),
                driveableY,
                &driveableSegmentId,
                seedSegmentId);
        }
        if (driveableFound)
        {
            return Tunables::kGripScaleOffroad;
        }
        return Tunables::kGripScaleFallback;
    }

    // Shared surface probe helper for body clips / adhesion.
    static bool QuerySurface(const ITrackCollisionQuery* trackQuery,
                             const Vector3D& worldPosition,
                             int32_t seedSegmentId,
                             SurfaceQueryResult& outResult,
                             bool includeSurfaceContact = false)
    {
        outResult = SurfaceQueryResult{};
        outResult.surfaceY = worldPosition.Y;
        if (!trackQuery)
        {
            return false;
        }

        SurfaceProbeSample surfaceSample{};
        if (TryProbeSurfaceY(trackQuery, worldPosition, seedSegmentId, surfaceSample))
        {
            outResult.valid = true;
            outResult.hasDriveableSupport = true;
            outResult.surfaceY = surfaceSample.y;
            outResult.segmentId = surfaceSample.segmentId;
        }

        Vector3D sampledNormal = Vector3D(0.0, -1.0, 0.0);
        int32_t sampledSegmentId = -1;
        const bool normalFound = trackQuery->Sample(worldPosition, sampledNormal, sampledSegmentId);
        if (normalFound)
        {
            outResult.valid = true;
            outResult.normal = sampledNormal;
            if (outResult.segmentId <= 0 && sampledSegmentId > 0)
            {
                outResult.segmentId = sampledSegmentId;
            }
        }

        // Contact metadata is optional for low-cost clip paths; query only when
        // explicitly requested or when no drivable height was found.
        if (includeSurfaceContact || !outResult.hasDriveableSupport)
        {
            const int32_t contactSeedSegmentId =
                (outResult.segmentId > 0) ? outResult.segmentId : seedSegmentId;
            Game::SurfaceContact contact{};
            const bool contactFound =
                trackQuery->SampleSurfaceContact(worldPosition, contact, contactSeedSegmentId) &&
                contact.valid;
            if (contactFound)
            {
                outResult.valid = true;
                outResult.faceIndex = contact.faceIndex;
                outResult.familyId = contact.familyId;
                outResult.surfaceType = contact.surfaceType;
                if (contact.segmentId > 0)
                {
                    outResult.segmentId = contact.segmentId;
                }
                if (!outResult.hasDriveableSupport)
                {
                    outResult.surfaceY = contact.surfaceY;
                }
                if (!normalFound)
                {
                    outResult.normal = contact.normal;
                }
            }
        }

        return outResult.valid;
    }

    static int32_t UpdateTarget(const ITrackCollisionQuery* trackQuery,
                                const Vector3D& worldPosition,
                                const FrameStepOutput& step,
                                GroundState& ioState,
                                GameplayFrameState& ioFrameState)
    {
        if (!trackQuery)
        {
            ioState.hasGroundSupport = false;
            ioState.edgeLeftLost = false;
            ioState.edgeRightLost = false;
            ioState.correctionX = Fxp::BuildRaw(0);
            ioState.correctionZ = Fxp::BuildRaw(0);
            return (ioState.lastSurfaceSegmentId > 0)
                ? static_cast<int32_t>(ioState.lastSurfaceSegmentId)
                : -1;
        }

        // Wheel-plane probes: 4 corners when enabled (pitch+roll from wheel heights).
        // Reduced = 2 axle centerline only (cheaper, pitch only).
        const bool useReducedProbe =
            !Tunables::kEnableFourWheelPlaneProbes ||
            Tunables::kForceAxleCenterlineProbes ||
            Tunables::kPreferReducedGroundProbe;
        ioState.surfaceProbeCooldown = 0u;

        int32_t sampledSegmentId = -1;
        {
            Vector3D sampledNormal{};
            (void)trackQuery->Sample(worldPosition, sampledNormal, sampledSegmentId);
        }

        ProbeSurfaceTarget(trackQuery,
                           worldPosition,
                           step.sinYaw,
                           step.cosYaw,
                           sampledSegmentId,
                           useReducedProbe,
                           ioState,
                           ioFrameState);

        if (sampledSegmentId <= 0 && ioState.lastSurfaceSegmentId > 0)
        {
            sampledSegmentId = static_cast<int32_t>(ioState.lastSurfaceSegmentId);
        }
        return sampledSegmentId;
    }

    // Store speed for continuous grade slide; optionally nudge Y so probes
    // land on the ramp (aligned path, not pure flat XZ).
    static void PredictYAlongGrade(GroundState& ioState,
                                   const Fxp& forwardSpeed,
                                   Vector3D& ioCarWorldPosition)
    {
        ioState.lastForwardSpeedRaw = forwardSpeed.RawValue();
        if constexpr (!Tunables::kEnableGradePredictY)
        {
            return;
        }
        if (!ioState.gradeValid || !ioState.surfaceYFilterInitialized)
        {
            return;
        }

        // dy ≈ tanθ · s_forward. Y-down: +tan · +speed ⇒ +Y (downhill).
        const int64_t dyRaw64 =
            (static_cast<int64_t>(ioState.gradeTanRaw) *
             static_cast<int64_t>(forwardSpeed.RawValue())) >> 16;
        int32_t dyRaw = static_cast<int32_t>(
            std::clamp<int64_t>(dyRaw64,
                                -static_cast<int64_t>(Tunables::kGradePredictYMax.RawValue()),
                                static_cast<int64_t>(Tunables::kGradePredictYMax.RawValue())));
        if (dyRaw == 0)
        {
            return;
        }
        ioCarWorldPosition.Y = Fxp::BuildRaw(ioCarWorldPosition.Y.RawValue() + dyRaw);
    }

    // Mild arcade gravity along the body longitudinal axis from road grade.
    // Does not project or kill planar speed (that path "empaca" on mild climbs).
    static void ApplySlopeGravity(GroundState& ioState, DynamicsState& ioDynamics)
    {
        if constexpr (!Tunables::kEnableSlopePathAssist)
        {
            return;
        }
        if (!ioState.gradeValid || !ioState.hasGroundSupport)
        {
            return;
        }

        int32_t tanRaw = ioState.gradeTanRaw;
        const int32_t tanMax = Tunables::kSlopeTanMax.RawValue();
        if (tanRaw > tanMax) tanRaw = tanMax;
        if (tanRaw < -tanMax) tanRaw = -tanMax;
        const int32_t tanAbs = (tanRaw < 0) ? -tanRaw : tanRaw;
        if (tanAbs < Tunables::kSlopeTanDeadzone.RawValue())
        {
            return;
        }

        // accel = g * tanθ  (body-frame: downhill forward when tan>0).
        const int64_t accelRaw64 =
            (static_cast<int64_t>(Tunables::kSlopeGravityPerFrame.RawValue()) *
             static_cast<int64_t>(tanRaw)) >> 16;
        const int32_t accelRaw = static_cast<int32_t>(
            std::clamp<int64_t>(accelRaw64, -(1 << 14), (1 << 14))); // ±0.25 hard cap
        if (accelRaw == 0)
        {
            return;
        }
        ioDynamics.forwardSpeed =
            Fxp::BuildRaw(ioDynamics.forwardSpeed.RawValue() + accelRaw);
        ioDynamics.forwardSpeed = Clamp(ioDynamics.forwardSpeed,
                                        Fxp::BuildRaw(-Tunables::kMaxReverseSpeed.RawValue()),
                                        Tunables::kMaxForwardSpeed);
    }

    static void ApplyVerticalAdhesion(GroundState& ioState, Vector3D& ioCarWorldPosition)
    {
        // Wall/edge planar push only — never freeze XZ when probes miss.
        // (Freezing lastStable XZ made the car "stuck" on declines when a probe
        // failed for a frame; REDRIVER2 MapHeight keeps planar motion free.)
        ioCarWorldPosition.X += ioState.correctionX;
        ioCarWorldPosition.Z += ioState.correctionZ;
        ioState.correctionX = Fxp::BuildRaw(0);
        ioState.correctionZ = Fxp::BuildRaw(0);

        if (!ioState.surfaceYInitialized)
        {
            // Hold last good Y for a few frames of miss instead of ungluing.
            if (ioState.surfaceContactFrames > 0u)
            {
                --ioState.surfaceContactFrames;
                ioCarWorldPosition.Y = ioState.surfaceYFiltered;
                ioState.verticalVelocity = Fxp::BuildRaw(
                    ioState.verticalVelocity.RawValue() >> 1);
            }
            else
            {
                ioState.verticalVelocity = Fxp::BuildRaw(0);
                ioState.surfaceYFilterInitialized = false;
            }
            return;
        }

        // Follow continuous slide target tightly on grade so body does not hold
        // flat between MapHeight slab samples (escada nas junções).
        const Fxp targetY = ioState.surfaceYTarget;
        if (!ioState.surfaceYFilterInitialized)
        {
            ioState.surfaceYFiltered = targetY;
            ioState.verticalVelocity = Fxp::BuildRaw(0);
            ioState.surfaceYFilterInitialized = true;
        }
        else
        {
            const int32_t cur = ioState.surfaceYFiltered.RawValue();
            const int32_t tgt = targetY.RawValue();
            int32_t velocity = ioState.verticalVelocity.RawValue();
            const bool sliding =
                Tunables::kEnableContinuousGradeSlide &&
                ioState.gradeValid &&
                (ioState.gradeTanRaw > Tunables::kTopoGradeDeclineMin.RawValue() ||
                 ioState.gradeTanRaw < -Tunables::kTopoGradeDeclineMin.RawValue());
            int32_t next = 0;
            // Adhesion (16-04): plant to target tightly both ways.
            // Previous climb-only 1/4 left body buried after continuous overshoot.
            {
                const int32_t error = tgt - cur;
                const int32_t maxUp = Tunables::kMaxBodySlideUpY.RawValue();
                const int32_t maxSlideDown = Tunables::kMaxBodySlideDownY.RawValue();
                const int32_t snapEps = Tunables::kHeaveSnapEpsY.RawValue();
                const int32_t spdAbs = (ioState.lastForwardSpeedRaw < 0)
                    ? -ioState.lastForwardSpeedRaw
                    : ioState.lastForwardSpeedRaw;
                const bool highSpeedPlant =
                    spdAbs >= Tunables::kHighSpeedPlantGlue.RawValue();
                int32_t step = 0;
                if (error <= snapEps && error >= -snapEps)
                {
                    next = tgt;
                    step = 0;
                }
                else if (error > 0)
                {
                    // Need deeper (float): plant hard — 3/4 residual always (16-17).
                    int32_t residual = (error * 3) >> 2;
                    if (highSpeedPlant ||
                        error > Tunables::kFloatCatchupY.RawValue())
                    {
                        residual = error; // full catch-up when clearly floating
                    }
                    if (residual == 0) residual = 1;
                    step = residual;
                    if (sliding)
                    {
                        int32_t gradeStep = ioState.surfaceTargetVelocityRaw;
                        if (gradeStep > step) step = gradeStep;
                    }
                    if (step > maxSlideDown) step = maxSlideDown;
                    next = cur + step;
                    if (next > tgt)
                    {
                        next = tgt;
                        step = tgt - cur;
                    }
                }
                else
                {
                    // Need higher (anti-bury): half residual, capped.
                    int32_t residual = error >> 1;
                    if (residual == 0) residual = -1;
                    step = residual;
                    if (step < -maxUp) step = -maxUp;
                    next = cur + step;
                    if (next < tgt)
                    {
                        next = tgt;
                        step = tgt - cur;
                    }
                }
                velocity = step;
            }
            ioState.verticalVelocity = Fxp::BuildRaw(velocity);
            ioState.surfaceYFiltered = Fxp::BuildRaw(next);
        }
        if (ioState.topologyDropFrames > 0u)
        {
            --ioState.topologyDropFrames;
        }
        ioCarWorldPosition.Y = ioState.surfaceYFiltered;
        ioState.surfaceContactFrames = 12u;

        if (ioState.hasGroundSupport)
        {
            ioState.lastStableX = ioCarWorldPosition.X;
            ioState.lastStableZ = ioCarWorldPosition.Z;
            ioState.lastStablePlanarInitialized = true;
            ioState.lastAcceptedSurfaceYRaw = ioState.surfaceYTarget.RawValue();
            ioState.lastAcceptedSurfaceYValid = true;
        }
    }

    static void Reset(GroundState& ioState)
    {
        ArcadeSuspensionFilter::Reset(ioState.suspension);
        CornerContactSolver::Reset(ioState.cornerSolver);
        ioState.solverBodyYRaw = 0;
        ioState.solverBodyYValid = false;
        ioState.surfaceYTarget = Fxp::BuildRaw(0);
        ioState.surfaceYFiltered = Fxp::BuildRaw(0);
        ioState.verticalVelocity = Fxp::BuildRaw(0);
        ioState.correctionX = Fxp::BuildRaw(0);
        ioState.correctionZ = Fxp::BuildRaw(0);
        ioState.lastStableX = Fxp::BuildRaw(0);
        ioState.lastStableZ = Fxp::BuildRaw(0);
        ioState.lastSurfaceSegmentId = -1;
        ioState.lastSurfaceFaceIndex = -1;
        ioState.lastSurfaceFamilyId = 0u;
        ioState.lastSurfaceType = 0u;
        ioState.surfaceProbeCooldown = 0u;
        ioState.auxProbeCooldown = 0u;
        ioState.surfaceContactCooldown = 0u;
        ioState.hasGroundSupport = false;
        ioState.edgeLeftLost = false;
        ioState.edgeRightLost = false;
        ioState.lastStablePlanarInitialized = false;
        ioState.surfaceYInitialized = false;
        ioState.surfaceYFilterInitialized = false;
        ioState.surfaceContactFrames = 0u;
        ioState.lastWallQueryFrameId = -1;
        ioState.lastWallApplyFrameId = -1;
        ioState.lastWallQueryHit = false;
        ioState.lastWallQuerySegmentId = -1;
        ioState.lastWallPushX = Fxp::BuildRaw(0);
        ioState.lastWallPushZ = Fxp::BuildRaw(0);
        ioState.lastSlopeAbsY = 0;
        ioState.gradeTanRaw = 0;
        ioState.gradeValid = false;
        ioState.gradeHoldFrames = 0u;
        ioState.gradeTanAttitudeRaw = 0;
        ioState.gradeAttitudeValid = false;
        ioState.gradeAttitudeHoldFrames = 0u;
        ioState.lastAttitudeChordRaw = 0;
        ioState.attitudeChordInitialized = false;
        ioState.lastAttitudeRollChordRaw = 0;
        ioState.attitudeRollChordInitialized = false;
        ioState.junctionAttitudeHoldFrames = 0u;
        ioState.lastForwardSpeedRaw = 0;
        ioState.lastMeasuredRideYRaw = 0;
        ioState.lastMeasuredRideYValid = false;
        ioState.smoothedRideYRaw = 0;
        ioState.smoothedRideYValid = false;
        ioState.lastAcceptedSurfaceYRaw = 0;
        ioState.lastAcceptedSurfaceYValid = false;
        ioState.committedSurfaceYRaw = 0;
        ioState.surfaceTargetVelocityRaw = 0;
        ioState.committedSurfaceYValid = false;
        ioState.topologyDropFrames = 0u;
    }

private:
    struct SurfaceProbeSample
    {
        Fxp y = Fxp::BuildRaw(0);
        int32_t segmentId = -1;
        int16_t faceIndex = -1;
        bool valid = false;
    };

    static bool TryProbeSurfaceY(const ITrackCollisionQuery* trackQuery,
                                 const Vector3D& worldPosition,
                                 int32_t seedSegmentId,
                                 SurfaceProbeSample& outSample,
                                 bool allowSoftFallback = true,
                                 int16_t hintFaceIndex = -1)
    {
        if (!trackQuery) return false;

        Vector3D samplePosition = worldPosition;
        samplePosition.Y -= Tunables::kSurfaceSampleDownBias;

        outSample.segmentId = -1;
        outSample.faceIndex = -1;
        if constexpr (Tunables::kEnableSurfaceTypeQuery)
        {
            if constexpr (Game::PhysicsFeatureFlags::kEnableWheelContactV2)
            {
                Game::SurfaceContact contact{};
                outSample.valid = trackQuery->SampleWheelSurfaceBySurfaceTypeSetStrict(
                    samplePosition,
                    Tunables::kDriveableSurfaceTypes.data(),
                    Tunables::kDriveableSurfaceTypes.size(),
                    contact,
                    seedSegmentId,
                    hintFaceIndex);
                if (outSample.valid)
                {
                    outSample.y = contact.surfaceY;
                    outSample.segmentId = contact.segmentId;
                    outSample.faceIndex = contact.faceIndex;
                }
            }
            else
            {
                outSample.valid = trackQuery->SampleSurfaceYBySurfaceTypeSetStrict(
                    samplePosition,
                    Tunables::kDriveableSurfaceTypes.data(),
                    Tunables::kDriveableSurfaceTypes.size(),
                    outSample.y,
                    &outSample.segmentId,
                    seedSegmentId);
            }
        }
        else
        {
            outSample.valid = trackQuery->SampleSurfaceYByFamilySetStrict(
                samplePosition,
                Tunables::kDriveableFamilies.data(),
                Tunables::kDriveableFamilies.size(),
                outSample.y,
                &outSample.segmentId,
                seedSegmentId);
        }
        if (outSample.valid) return true;
        if (!allowSoftFallback) return false;

        // Soft fallback: accept non-strict result only when still near seed.
        SRL::Math::Types::Fxp fallbackY{};
        int32_t fallbackSegmentId = -1;
        bool fallbackFound = false;
        if constexpr (Tunables::kEnableSurfaceTypeQuery)
        {
            fallbackFound = trackQuery->SampleSurfaceYBySurfaceTypeSet(
                samplePosition,
                Tunables::kDriveableSurfaceTypes.data(),
                Tunables::kDriveableSurfaceTypes.size(),
                fallbackY,
                &fallbackSegmentId,
                seedSegmentId);
        }
        else
        {
            fallbackFound = trackQuery->SampleSurfaceYByFamilySet(
                samplePosition,
                Tunables::kDriveableFamilies.data(),
                Tunables::kDriveableFamilies.size(),
                fallbackY,
                &fallbackSegmentId,
                seedSegmentId);
        }
        if (fallbackFound)
        {
            const uint8_t affinity = SegmentAffinityScore(fallbackSegmentId, seedSegmentId);
            if (affinity > 0u || seedSegmentId <= 0)
            {
                outSample.y = fallbackY;
                outSample.segmentId = fallbackSegmentId;
                outSample.valid = true;
                return true;
            }
        }
        return false;
    }

    static Vector3D BuildProbePoint(const Vector3D& worldPosition,
                                    const Fxp& sinYaw,
                                    const Fxp& cosYaw,
                                    const Fxp& longitudinalOffset,
                                    const Fxp& lateralOffset)
    {
        const Fxp forwardX = sinYaw;
        const Fxp forwardZ = Fxp::BuildRaw(-cosYaw.RawValue());
        const Fxp rightX = cosYaw;
        const Fxp rightZ = sinYaw;

        Vector3D p = worldPosition;
        p.X += (forwardX * longitudinalOffset) + (rightX * lateralOffset);
        p.Z += (forwardZ * longitudinalOffset) + (rightZ * lateralOffset);
        return p;
    }

    static bool HasProbeSupport(const SurfaceProbeSample& a, const SurfaceProbeSample& b)
    {
        return a.valid || b.valid;
    }

    static bool ResolveWallPushMultiProbe(const ITrackCollisionQuery* trackQuery,
                                          const Vector3D& basePosition,
                                          const Fxp& sinYaw,
                                          const Fxp& cosYaw,
                                          Fxp wallRadius,
                                          int32_t seedSegmentId,
                                          Vector3D& outPush,
                                          int16_t* outSegmentId)
    {
        outPush = Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(0));
        if (!trackQuery)
        {
            if (outSegmentId) *outSegmentId = -1;
            return false;
        }

        const Vector3D forwardDirection(
            sinYaw,
            Fxp::BuildRaw(0),
            Fxp::BuildRaw(-cosYaw.RawValue()));
        const Fxp wallRightX = cosYaw;
        const Fxp wallRightZ = sinYaw;

        int32_t localSeedSegmentId = seedSegmentId;
        int32_t lastHitSegmentId = -1;
        bool anyHit = false;
        Vector3D workingBase = basePosition;

        constexpr uint8_t kMaxResolvePasses =
            Tunables::kEnableSaturnLowCostPhysics ? 1u : 3u;
        const Fxp probeHalfLength = Tunables::kWallHullHalfLength;
        const Fxp probeHalfWidth = Tunables::kWallHullHalfWidth;
        const Fxp negHalfLength = Fxp::BuildRaw(-probeHalfLength.RawValue());
        const Fxp negHalfWidth = Fxp::BuildRaw(-probeHalfWidth.RawValue());
        const Fxp probeRadius = Tunables::kEnableSaturnLowCostPhysics
            ? Tunables::kWallHullProbeRadius
            : wallRadius;
        const Fxp frontProbeLength = Tunables::kEnableSaturnLowCostPhysics
            ? (probeHalfLength + probeRadius)
            : probeHalfLength;
        const std::pair<Fxp, Fxp> probeOffsets[6] = {
            { frontProbeLength, negHalfWidth },
            { frontProbeLength, probeHalfWidth },
            { negHalfLength, negHalfWidth },
            { negHalfLength, probeHalfWidth },
            { Fxp::BuildRaw(0), negHalfWidth },
            { Fxp::BuildRaw(0), probeHalfWidth }
        };
        // Cost cut micro-step: front L/R only (+ center fallback below).
        // Rollback: restore 4u if wall clips under throttle/curve.
        const uint8_t probeCount =
            Tunables::kEnableSaturnLowCostPhysics ? 2u : 6u;

        for (uint8_t pass = 0; pass < kMaxResolvePasses; ++pass)
        {
            bool passHit = false;
            Vector3D bestPush(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(0));
            int32_t bestSegmentId = -1;
            int64_t bestMagRaw = -1;

            for (uint8_t probeIndex = 0; probeIndex < probeCount; ++probeIndex)
            {
                const auto& probeOffset = probeOffsets[probeIndex];
                Vector3D probePosition = workingBase;
                probePosition.X +=
                    (forwardDirection.X * probeOffset.first) +
                    (wallRightX * probeOffset.second);
                probePosition.Z +=
                    (forwardDirection.Z * probeOffset.first) +
                    (wallRightZ * probeOffset.second);

                Vector3D probePush{};
                int32_t probeSegmentId = -1;
                const bool hit = trackQuery->ResolvePlanarWallPush(
                    probePosition,
                    forwardDirection,
                    probeRadius,
                    probePush,
                    &probeSegmentId,
                    localSeedSegmentId);
                if (!hit)
                {
                    continue;
                }

                passHit = true;
                anyHit = true;
                const int64_t magRaw =
                    static_cast<int64_t>(probePush.X.Abs().RawValue()) +
                    static_cast<int64_t>(probePush.Z.Abs().RawValue());
                if (magRaw > bestMagRaw)
                {
                    bestMagRaw = magRaw;
                    bestPush = probePush;
                    bestSegmentId = probeSegmentId;
                }
            }

            if constexpr (Tunables::kEnableSaturnLowCostPhysics)
            {
                if (!passHit)
                {
                    Vector3D centerPush{};
                    int32_t centerSegmentId = -1;
                    const bool centerHit = trackQuery->ResolvePlanarWallPush(
                        workingBase,
                        forwardDirection,
                        wallRadius,
                        centerPush,
                        &centerSegmentId,
                        localSeedSegmentId);
                    if (centerHit)
                    {
                        passHit = true;
                        anyHit = true;
                        bestPush = centerPush;
                        bestSegmentId = centerSegmentId;
                    }
                }
            }

            if (!passHit)
            {
                break;
            }

            outPush.X += bestPush.X;
            outPush.Z += bestPush.Z;
            workingBase.X += bestPush.X;
            workingBase.Z += bestPush.Z;
            if (bestSegmentId > 0)
            {
                localSeedSegmentId = bestSegmentId;
                lastHitSegmentId = bestSegmentId;
            }
        }

        if (outSegmentId)
        {
            *outSegmentId = static_cast<int16_t>(lastHitSegmentId);
        }
        return anyHit;
    }

    static Fxp AveragePairY(const SurfaceProbeSample& a,
                            const SurfaceProbeSample& b,
                            const Fxp& fallback)
    {
        if (a.valid && b.valid) return (a.y + b.y) / 2;
        if (a.valid) return a.y;
        if (b.valid) return b.y;
        return fallback;
    }

    static int32_t ResolveSegmentId(const SurfaceProbeSample& fl,
                                    const SurfaceProbeSample& fr,
                                    const SurfaceProbeSample& rl,
                                    const SurfaceProbeSample& rr,
                                    int32_t fallbackSegmentId)
    {
        if (rl.valid) return rl.segmentId;
        if (rr.valid) return rr.segmentId;
        if (fl.valid) return fl.segmentId;
        if (fr.valid) return fr.segmentId;
        return fallbackSegmentId;
    }

    // Wheel-plane MapHeight (arcade / REDRIVER2-inspired):
    //   4 corners FL/FR/RL/RR → body height = avg; pitch from F−R; roll from R−L.
    //   Reduced mode: 2 axle centerline samples (pitch only, cheaper).
    static void ProbeSurfaceTarget(const ITrackCollisionQuery* trackQuery,
                                   const Vector3D& worldPosition,
                                   const Fxp& sinYaw,
                                   const Fxp& cosYaw,
                                   int32_t sampledSegmentId,
                                   bool useReducedProbe,
                                   GroundState& ioState,
                                   GameplayFrameState& ioFrameState)
    {
        ResetGroundDebug(ioFrameState);
        ioState.hasGroundSupport = false;
        ioState.edgeLeftLost = false;
        ioState.edgeRightLost = false;
        ioState.correctionX = Fxp::BuildRaw(0);
        ioState.correctionZ = Fxp::BuildRaw(0);

        const int32_t seedSegmentId = (sampledSegmentId > 0)
            ? sampledSegmentId
            : ((ioState.lastSurfaceSegmentId > 0)
                   ? static_cast<int32_t>(ioState.lastSurfaceSegmentId)
                   : -1);

        const Fxp longFront = Tunables::kProbeHalfWheelBase;
        const Fxp longRear = Fxp::BuildRaw(-Tunables::kProbeHalfWheelBase.RawValue());
        const Fxp latLeft = Fxp::BuildRaw(-Tunables::kProbeHalfTrack.RawValue());
        const Fxp latRight = Tunables::kProbeHalfTrack;

        SurfaceProbeSample fl{};
        SurfaceProbeSample fr{};
        SurfaceProbeSample rl{};
        SurfaceProbeSample rr{};

        ArcadeSuspensionFilter::BeginFrame(ioState.suspension);
        // Live targets every sample — staging diagonals made the plane jump
        // every 2 frames (escada). Junction probes use seed±1.
        uint8_t sampledWheelMask = 0u;
        auto sampleWheel = [&](uint8_t wheelIndex,
                               const Fxp& longitudinal,
                               const Fxp& lateral)
        {
            SurfaceProbeSample sample{};
            int32_t wheelSeedSegmentId = seedSegmentId;
            int16_t wheelHintFaceIndex = -1;
            const uint8_t wheelBit = static_cast<uint8_t>(1u << wheelIndex);
            if ((ioState.suspension.validMask & wheelBit) != 0u)
            {
                if (ioState.suspension.segmentIds[wheelIndex] > 0)
                {
                    wheelSeedSegmentId = ioState.suspension.segmentIds[wheelIndex];
                }
                wheelHintFaceIndex = ioState.suspension.faceIndices[wheelIndex];
            }
            auto probeAt = [&](const Fxp& longOff,
                               const Fxp& latOff,
                               int32_t seedId) -> bool
            {
                sample = {};
                return TryProbeSurfaceY(
                    trackQuery,
                    BuildProbePoint(worldPosition, sinYaw, cosYaw, longOff, latOff),
                    seedId,
                    sample,
                    !Tunables::kEnableWheelStrictSurface,
                    wheelHintFaceIndex) && sample.valid;
            };
            bool hit = probeAt(longitudinal, lateral, wheelSeedSegmentId);
            if (!hit)
            {
                const Fxp latIn = Fxp::BuildRaw(lateral.RawValue() / 2);
                hit = probeAt(longitudinal, latIn, wheelSeedSegmentId);
            }
            // Cross segment junction: try next/prev belt segment immediately.
            if (!hit && wheelSeedSegmentId > 0)
            {
                hit = probeAt(longitudinal, lateral, wheelSeedSegmentId + 1);
                if (!hit && wheelSeedSegmentId > 1)
                {
                    hit = probeAt(longitudinal, lateral, wheelSeedSegmentId - 1);
                }
            }
            if (hit)
            {
                if (ArcadeSuspensionFilter::Observe(
                        ioState.suspension,
                        wheelIndex,
                        sample.y.RawValue(),
                        sample.segmentId,
                        sample.faceIndex))
                {
                    sampledWheelMask = static_cast<uint8_t>(
                        sampledWheelMask | wheelBit);
                }
            }
        };
        if (useReducedProbe)
        {
            if ((ioState.suspension.diagonalPhase & 1u) == 0u)
            {
                sampleWheel(0u, longFront, latLeft);
                sampleWheel(3u, longRear, latRight);
            }
            else
            {
                sampleWheel(1u, longFront, latRight);
                sampleWheel(2u, longRear, latLeft);
            }
            ioState.suspension.diagonalPhase ^= 1u;
            // Advance corners not sampled this frame along last grade velocity so
            // the 4-wheel mean stays on the face (diagonal would otherwise lag
            // and leave air / wrong pitch — videos 14-36 / 14-38).
            int32_t dyHold = ioState.surfaceTargetVelocityRaw;
            const int32_t maxDy = Tunables::kMaxBodySlideDownY.RawValue();
            const int32_t maxUp = Tunables::kMaxYStepUpPerFrame.RawValue();
            if (dyHold > maxDy) dyHold = maxDy;
            if (dyHold < -maxUp) dyHold = -maxUp;
            if (dyHold != 0)
            {
                for (uint8_t i = 0u; i < 4u; ++i)
                {
                    const uint8_t bit = static_cast<uint8_t>(1u << i);
                    if ((sampledWheelMask & bit) != 0u) continue;
                    if ((ioState.suspension.validMask & bit) == 0u) continue;
                    ioState.suspension.targetYRaw[i] += dyHold;
                    ioState.suspension.filteredYRaw[i] += dyHold;
                }
            }
        }
        else
        {
            sampleWheel(0u, longFront, latLeft);
            sampleWheel(1u, longFront, latRight);
            sampleWheel(2u, longRear, latLeft);
            sampleWheel(3u, longRear, latRight);
        }

        ArcadeSuspensionFilter::StepWheels(ioState.suspension);
        auto restoreWheel = [&](uint8_t wheelIndex, SurfaceProbeSample& sample)
        {
            int32_t yRaw = 0;
            int32_t segmentId = -1;
            int32_t faceIndex = -1;
            // Targets (not spring lag) for attitude + continuous heave plane.
            sample.valid = ArcadeSuspensionFilter::Read(ioState.suspension,
                                                        wheelIndex,
                                                        yRaw,
                                                        segmentId,
                                                        &faceIndex);
            if (!sample.valid) return;
            // Prefer target Y when available for plane (less spring lag stairs).
            yRaw = ioState.suspension.targetYRaw[wheelIndex];
            sample.y = Fxp::BuildRaw(yRaw);
            sample.segmentId = segmentId;
            sample.faceIndex = static_cast<int16_t>(faceIndex);
        };
        restoreWheel(0u, fl);
        restoreWheel(1u, fr);
        restoreWheel(2u, rl);
        restoreWheel(3u, rr);

        const bool frontValid = HasProbeSupport(fl, fr);
        const bool rearValid = HasProbeSupport(rl, rr);
        const bool leftValid = HasProbeSupport(fl, rl);
        const bool rightValid = HasProbeSupport(fr, rr);
        ioState.hasGroundSupport = frontValid || rearValid;

        if (rearValid) ioFrameState.debugGroundMask |= 0x1u;
        if (leftValid) ioFrameState.debugGroundMask |= 0x2u;
        if (frontValid) ioFrameState.debugGroundMask |= 0x4u;
        if (rightValid) ioFrameState.debugGroundMask |= 0x8u;
        if (fl.valid) ioFrameState.debugGroundMask |= 0x10u;
        if (fr.valid) ioFrameState.debugGroundMask |= 0x20u;
        if (rl.valid) ioFrameState.debugGroundMask |= 0x40u;
        if (rr.valid) ioFrameState.debugGroundMask |= 0x80u;

        auto publishWheel = [&](const SurfaceProbeSample& sample,
                                int16_t& outSurfY,
                                int16_t& outDist)
        {
            if (!sample.valid)
            {
                outSurfY = 0;
                outDist = 0;
                return;
            }
            outSurfY = FxpToDebugInt(sample.y);
            outDist = FxpToDebugInt(sample.y - worldPosition.Y);
        };
        publishWheel(fl, ioFrameState.debugWheelSurfYFl, ioFrameState.debugWheelDistFl);
        publishWheel(fr, ioFrameState.debugWheelSurfYFr, ioFrameState.debugWheelDistFr);
        publishWheel(rl, ioFrameState.debugWheelSurfYRl, ioFrameState.debugWheelDistRl);
        publishWheel(rr, ioFrameState.debugWheelSurfYRr, ioFrameState.debugWheelDistRr);

        // Incomplete F+R: keep last heave/grade (anti-float / anti-escada).
        if (!frontValid || !rearValid)
        {
            int32_t stickyPlaneY = 0;
            if (ArcadeSuspensionFilter::AverageTargetYRaw(
                    ioState.suspension, stickyPlaneY))
            {
                // Still advance along held grade so we slide through a miss.
                int32_t ride = stickyPlaneY + GetRideHeightOffset().RawValue();
                if (Tunables::kEnableContinuousGradeSlide &&
                    ioState.gradeValid &&
                    ioState.surfaceYInitialized)
                {
                    const int64_t dy64 =
                        (static_cast<int64_t>(ioState.gradeTanRaw) *
                         static_cast<int64_t>(ioState.lastForwardSpeedRaw)) >> 16;
                    // Clamp: negative = climb (up), positive = descend (Y-down).
                    int32_t dy = static_cast<int32_t>(std::clamp<int64_t>(
                        dy64,
                        -static_cast<int64_t>(Tunables::kMaxYStepUpPerFrame.RawValue()),
                        static_cast<int64_t>(Tunables::kMaxYStepDownPerFrame.RawValue())));
                    ride = ioState.surfaceYTarget.RawValue() + dy;
                }
                ioState.surfaceYTarget = Fxp::BuildRaw(ride);
                ioState.surfaceYInitialized = true;
                ioState.hasGroundSupport = true;
            }
            else
            {
                ioState.hasGroundSupport = ioState.surfaceYFilterInitialized;
            }
            ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYTarget);
            ioFrameState.debugGroundYBody = FxpToDebugInt(ioState.surfaceYFiltered);
            return;
        }
        ioState.hasGroundSupport = true;

        // Do NOT fall back to body Y for a missing corner — that equalizes
        // front/rear and snaps attitude upright. Use only valid corners;
        // AveragePairY with zero fallback is only used when pair has a hit
        // (HasProbeSupport already true for the pair).
        const Fxp zeroY = Fxp::BuildRaw(0);
        const Fxp frontY = AveragePairY(fl, fr, zeroY);
        const Fxp rearY = AveragePairY(rl, rr, zeroY);
        const Fxp leftY = AveragePairY(fl, rl, zeroY);
        const Fxp rightY = AveragePairY(fr, rr, zeroY);

        // Single-plant attitude (plan 2026-08-08):
        // mid-face = live F−R/R−L rate-limited; junction = gradeChord only.
        // Solver disabled — stacked systems caused fly/jump/embicada.
        int32_t pubFrontRaw = frontValid ? frontY.RawValue() : 0;
        int32_t pubRearRaw = rearValid ? rearY.RawValue() : 0;
        int32_t pubLeftRaw = leftValid ? leftY.RawValue() : 0;
        int32_t pubRightRaw = rightValid ? rightY.RawValue() : 0;
        bool axleJunction = false;
        ioState.solverBodyYValid = false;

        auto axleSeg = [](const SurfaceProbeSample& a,
                          const SurfaceProbeSample& b) -> int32_t
        {
            if (a.valid && a.segmentId > 0) return a.segmentId;
            if (b.valid && b.segmentId > 0) return b.segmentId;
            return -1;
        };
        const int32_t frontSeg =
            (frontValid) ? axleSeg(fl, fr) : -1;
        const int32_t rearSeg =
            (rearValid) ? axleSeg(rl, rr) : -1;
        // Split = axles on different segments this frame (true junction).
        // Soft-exit = brief crawl after reunite. Do NOT long-freeze on
        // |raw−grade| lag (15-42 crawl: freeze left pitch stale vs ground).
        if (frontSeg > 0 && rearSeg > 0 && frontSeg != rearSeg)
        {
            axleJunction = true;
            ioState.junctionAttitudeHoldFrames =
                Tunables::kJunctionAttitudeHoldFrames;
        }

        const int32_t wb =
            Tunables::kProbeHalfWheelBase.RawValue() << 1;
        const int32_t maxChord = Tunables::kMaxAttitudeChordY.RawValue();
        int32_t gradeChord = 0;
        bool haveGradeChord = false;
        // Prefer heave grade (stable); attitude channel can lag/spike.
        if (ioState.gradeValid && wb > 0)
        {
            gradeChord = static_cast<int32_t>(
                (static_cast<int64_t>(ioState.gradeTanRaw) *
                 static_cast<int64_t>(wb)) >> 16);
            if (gradeChord > maxChord) gradeChord = maxChord;
            if (gradeChord < -maxChord) gradeChord = -maxChord;
            haveGradeChord = true;
        }
        else if (ioState.gradeAttitudeValid && wb > 0)
        {
            gradeChord = static_cast<int32_t>(
                (static_cast<int64_t>(ioState.gradeTanAttitudeRaw) *
                 static_cast<int64_t>(wb)) >> 16);
            if (gradeChord > maxChord) gradeChord = maxChord;
            if (gradeChord < -maxChord) gradeChord = -maxChord;
            haveGradeChord = true;
        }

        // Spike soft-exit only (one-shot arm) — not a 32-frame pitch freeze.
        if (frontValid && rearValid && !axleJunction &&
            ioState.attitudeChordInitialized)
        {
            const int32_t rawChord = pubFrontRaw - pubRearRaw;
            const int32_t jump = rawChord - ioState.lastAttitudeChordRaw;
            const int32_t jumpAbs = (jump < 0) ? -jump : jump;
            if (jumpAbs > Tunables::kChordJumpArmY.RawValue() &&
                ioState.junctionAttitudeHoldFrames < 2u)
            {
                ioState.junctionAttitudeHoldFrames = 2u;
            }
        }
        // Soft-exit countdown only when axles are reunited.
        if (!axleJunction && ioState.junctionAttitudeHoldFrames > 0u)
        {
            --ioState.junctionAttitudeHoldFrames;
        }
        const bool inJunctionHold =
            axleJunction || (ioState.junctionAttitudeHoldFrames > 0u);
        const bool axleSplitNow = axleJunction;

        bool usedCornerSolver = false;
        if constexpr (Tunables::kEnableCornerContactSolver)
        {
            if (fl.valid && fr.valid && rl.valid && rr.valid)
            {
                int32_t surfaceY[4] = {
                    fl.y.RawValue(), fr.y.RawValue(),
                    rl.y.RawValue(), rr.y.RawValue()
                };
                const int32_t rawFrontAvg = static_cast<int32_t>(
                    (static_cast<int64_t>(surfaceY[0]) + surfaceY[1]) >> 1);
                const int32_t rawRearAvg = static_cast<int32_t>(
                    (static_cast<int64_t>(surfaceY[2]) + surfaceY[3]) >> 1);
                const int32_t rawMid = static_cast<int32_t>(
                    (static_cast<int64_t>(rawFrontAvg) + rawRearAvg) >> 1);

                // Senna-ref plant (21-06 / 21-08):
                // - Mid-face: LIVE F−R / R−L from MapHeight (align body to face).
                // - Junction only: replace pitch with continuous grade (anti embicada).
                // Heave always from rawMid (true 4-wheel avg).
                int32_t planePitch = rawFrontAvg - rawRearAvg;
                int32_t planeRoll = static_cast<int32_t>(
                    (static_cast<int64_t>(surfaceY[1]) + surfaceY[3] -
                     surfaceY[0] - surfaceY[2]) >> 1);

                if (inJunctionHold && haveGradeChord)
                {
                    // Virtual step F−R: use path grade for pitch only.
                    planePitch = gradeChord;
                    // Rebuild contacts on continuous plane around rawMid (heave
                    // stays on real average — no rear-only float).
                    const int32_t halfP = planePitch >> 1;
                    const int32_t halfR = planeRoll >> 1;
                    surfaceY[0] = rawMid + halfP - halfR;
                    surfaceY[1] = rawMid + halfP + halfR;
                    surfaceY[2] = rawMid - halfP - halfR;
                    surfaceY[3] = rawMid - halfP + halfR;
                }
                else if (haveGradeChord &&
                         planePitch > gradeChord +
                             Tunables::kEmbicadaOverGradeY.RawValue())
                {
                    // Cap embicada spike without flattening the real face.
                    planePitch = gradeChord +
                        Tunables::kEmbicadaOverGradeY.RawValue();
                    const int32_t halfP = planePitch >> 1;
                    const int32_t halfR = planeRoll >> 1;
                    surfaceY[0] = rawMid + halfP - halfR;
                    surfaceY[1] = rawMid + halfP + halfR;
                    surfaceY[2] = rawMid - halfP - halfR;
                    surfaceY[3] = rawMid - halfP + halfR;
                }
                // else: leave surfaceY as live MapHeight — full face tracking.

                const int32_t rideOff = GetRideHeightOffset().RawValue();
                const int32_t heaveMid = rawMid;
                int32_t bodyY = heaveMid + rideOff;
                if (ioState.surfaceYFilterInitialized)
                {
                    bodyY = ioState.surfaceYFiltered.RawValue();
                }
                CornerContactSolverOutput solverOut{};
                if (CornerContactSolver::Step(
                        surfaceY,
                        0x0Fu,
                        rideOff,
                        bodyY,
                        ioState.cornerSolver,
                        &solverOut))
                {
                    usedCornerSolver = true;
                    // Heave = live asphalt average (anti-fly).
                    ioState.solverBodyYRaw = heaveMid + rideOff;
                    ioState.solverBodyYValid = true;
                    (void)solverOut;
                    (void)bodyY;

                    // Prefer solver deltas (track live plane); junction forced grade.
                    int32_t pitchD = inJunctionHold && haveGradeChord
                        ? gradeChord
                        : ioState.cornerSolver.pitchDeltaRaw;
                    int32_t rollD = ioState.cornerSolver.rollDeltaRaw;

                    // Soft rate-limit only (ref car follows road without lag).
                    if (ioState.attitudeChordInitialized)
                    {
                        const int32_t prev = ioState.lastAttitudeChordRaw;
                        int32_t jump = pitchD - prev;
                        const int32_t maxDive = inJunctionHold
                            ? Tunables::kJunctionMaxChordStepY.RawValue()
                            : Tunables::kMaxAttitudeChordStepDiveY.RawValue();
                        const int32_t maxRec =
                            Tunables::kMaxAttitudeChordStepY.RawValue();
                        if (jump > maxDive) jump = maxDive;
                        else if (jump < -maxRec) jump = -maxRec;
                        pitchD = prev + jump;
                    }
                    if (haveGradeChord &&
                        pitchD > gradeChord +
                            Tunables::kEmbicadaOverGradeY.RawValue())
                    {
                        pitchD = gradeChord +
                            Tunables::kEmbicadaOverGradeY.RawValue();
                    }

                    if (ioState.attitudeRollChordInitialized)
                    {
                        const int32_t prevR = ioState.lastAttitudeRollChordRaw;
                        int32_t jumpR = rollD - prevR;
                        const int32_t maxR =
                            Tunables::kMaxAttitudeChordStepY.RawValue();
                        if (jumpR > maxR) jumpR = maxR;
                        if (jumpR < -maxR) jumpR = -maxR;
                        rollD = prevR + jumpR;
                    }

                    ioState.cornerSolver.pitchDeltaRaw = pitchD;
                    ioState.cornerSolver.rollDeltaRaw = rollD;
                    ioState.lastAttitudeChordRaw = pitchD;
                    ioState.attitudeChordInitialized = true;
                    ioState.lastAttitudeRollChordRaw = rollD;
                    ioState.attitudeRollChordInitialized = true;

                    const int32_t halfP = pitchD >> 1;
                    const int32_t halfR = rollD >> 1;
                    pubFrontRaw = heaveMid + halfP;
                    pubRearRaw = heaveMid - halfP;
                    pubLeftRaw = heaveMid - halfR;
                    pubRightRaw = heaveMid + halfR;
                }
            }
        }

        // Attitude: continuous grade plane (15-42/15-44).
        // - Split: freeze pitch (grade also frozen) — no embicada from F−R stair.
        // - Else: always track gradeChord with tiny residual (no long freeze).
        const int32_t rawAxleFrontY = pubFrontRaw;
        const int32_t rawAxleRearY = pubRearRaw;
        if (!usedCornerSolver && frontValid && rearValid)
        {
            int32_t rawChord = pubFrontRaw - pubRearRaw;
            if (rawChord > maxChord) rawChord = maxChord;
            if (rawChord < -maxChord) rawChord = -maxChord;

            int32_t targetChord = rawChord;
            if (axleSplitNow)
            {
                // True split: freeze last (grade held too).
                if (ioState.attitudeChordInitialized)
                {
                    targetChord = ioState.lastAttitudeChordRaw;
                }
                else if (haveGradeChord)
                {
                    targetChord = gradeChord;
                }
            }
            else if (haveGradeChord)
            {
                // Align to face: blend grade + live F−R (16-17 pitch lag / high).
                // Spikes beyond margin still capped (anti embicada).
                const int32_t over = rawChord - gradeChord;
                const int32_t overAbs = (over < 0) ? -over : over;
                const int32_t margin = Tunables::kEmbicadaOverGradeY.RawValue();
                if (overAbs > margin)
                {
                    targetChord = gradeChord +
                        ((over > 0) ? margin : -margin);
                }
                else
                {
                    // Half grade + half raw face (stronger alignment).
                    targetChord = gradeChord +
                        (over >> Tunables::kPitchGradeBlendShift);
                }
            }

            if (!ioState.attitudeChordInitialized)
            {
                ioState.lastAttitudeChordRaw =
                    haveGradeChord ? gradeChord : targetChord;
                ioState.attitudeChordInitialized = true;
            }
            else
            {
                const int32_t prev = ioState.lastAttitudeChordRaw;
                int32_t jump = targetChord - prev;
                // Split: freeze. Soft-exit: crawl. Face: rate-limit + blend.
                int32_t maxStep = Tunables::kMaxAttitudeChordStepDiveY.RawValue();
                if (axleSplitNow)
                {
                    maxStep = 0;
                }
                else if (inJunctionHold)
                {
                    maxStep = Tunables::kJunctionMaxChordStepY.RawValue();
                }
                if (jump > maxStep) jump = maxStep;
                if (jump < -maxStep) jump = -maxStep;
                if (!axleSplitNow && jump != 0)
                {
                    jump >>= Tunables::kAttitudeChordBlendShift;
                    if (jump == 0 && (targetChord - prev) != 0)
                    {
                        jump = (targetChord > prev) ? 1 : -1;
                    }
                }
                ioState.lastAttitudeChordRaw = prev + jump;
            }

            // Mid: continuous surface target at split (no raw axle stair).
            const int32_t chord = ioState.lastAttitudeChordRaw;
            int32_t mid = static_cast<int32_t>(
                (static_cast<int64_t>(rawAxleFrontY) + rawAxleRearY) >> 1);
            if (axleSplitNow && ioState.surfaceYInitialized)
            {
                mid = ioState.surfaceYTarget.RawValue() -
                      GetRideHeightOffset().RawValue();
            }
            const int32_t half = chord >> 1;
            pubFrontRaw = mid + half;
            pubRearRaw = mid - half;
        }

        if (!usedCornerSolver && leftValid && rightValid)
        {
            int32_t rollChord = pubRightRaw - pubLeftRaw;
            if (rollChord > maxChord) rollChord = maxChord;
            if (rollChord < -maxChord) rollChord = -maxChord;
            if (!ioState.attitudeRollChordInitialized)
            {
                ioState.lastAttitudeRollChordRaw = rollChord;
                ioState.attitudeRollChordInitialized = true;
            }
            else
            {
                const int32_t prev = ioState.lastAttitudeRollChordRaw;
                int32_t targetRoll = rollChord;
                if (axleSplitNow)
                {
                    targetRoll = prev; // freeze roll on split
                }
                int32_t jump = targetRoll - prev;
                int32_t maxStep = Tunables::kMaxAttitudeChordStepY.RawValue();
                if (axleSplitNow)
                {
                    maxStep = 0;
                }
                else if (inJunctionHold)
                {
                    maxStep = Tunables::kJunctionMaxChordStepY.RawValue();
                }
                if (jump > maxStep) jump = maxStep;
                if (jump < -maxStep) jump = -maxStep;
                if (!axleSplitNow && jump != 0)
                {
                    jump >>= Tunables::kAttitudeChordBlendShift;
                    if (jump == 0 && (targetRoll - prev) != 0)
                    {
                        jump = (targetRoll > prev) ? 1 : -1;
                    }
                }
                ioState.lastAttitudeRollChordRaw = prev + jump;
            }
            const int32_t midL = static_cast<int32_t>(
                (static_cast<int64_t>(leftY.RawValue()) + rightY.RawValue()) >> 1);
            const int32_t halfR = ioState.lastAttitudeRollChordRaw >> 1;
            pubLeftRaw = midL - halfR;
            pubRightRaw = midL + halfR;
        }

        ioFrameState.debugGroundYFront =
            frontValid ? FxpToDebugInt(Fxp::BuildRaw(pubFrontRaw)) : 0;
        ioFrameState.debugGroundYRear =
            rearValid ? FxpToDebugInt(Fxp::BuildRaw(pubRearRaw)) : 0;
        ioFrameState.debugGroundYLeft =
            leftValid ? FxpToDebugInt(Fxp::BuildRaw(pubLeftRaw)) : 0;
        ioFrameState.debugGroundYRight =
            rightValid ? FxpToDebugInt(Fxp::BuildRaw(pubRightRaw)) : 0;
        ioFrameState.debugGroundYFrontRaw = frontValid ? pubFrontRaw : 0;
        ioFrameState.debugGroundYRearRaw = rearValid ? pubRearRaw : 0;
        ioFrameState.debugGroundYLeftRaw = leftValid ? pubLeftRaw : 0;
        ioFrameState.debugGroundYRightRaw = rightValid ? pubRightRaw : 0;

        // Fit one rigid plane to the four filtered contacts. Pitch and roll
        // move the chassis; wheel meshes consume only the non-planar residual.
        // This avoids applying the same grade once to the body and again as
        // suspension travel. Q8.8 retains sub-unit precision in eight bytes.
        if (fl.valid && fr.valid && rl.valid && rr.valid)
        {
            const std::array<int32_t, 4> contactRaw{{
                fl.y.RawValue(), fr.y.RawValue(),
                rl.y.RawValue(), rr.y.RawValue()
            }};
            std::array<int32_t, 4> residualRaw{{0, 0, 0, 0}};
            ArcadeSuspensionFilter::FitContactPlaneResiduals(
                contactRaw, residualRaw);
            int16_t* residualOut[4] = {
                &ioFrameState.debugWheelResidualFlX256,
                &ioFrameState.debugWheelResidualFrX256,
                &ioFrameState.debugWheelResidualRlX256,
                &ioFrameState.debugWheelResidualRrX256
            };
            for (uint8_t i = 0u; i < 4u; ++i)
            {
                const int32_t residualX256 = residualRaw[i] >> 8;
                *residualOut[i] = static_cast<int16_t>(
                    std::clamp<int32_t>(residualX256, -32768, 32767));
            }
        }

        ioState.lastSlopeAbsY = 0;
        if (frontValid && rearValid)
        {
            // Grade from RAW axle MapHeight only when same segment — never from
            // attitude-capped chord (circular) or split-axle spike (14-51).
            const int32_t rawChordForGrade = rawAxleFrontY - rawAxleRearY;
            int32_t d = rawChordForGrade >> 16;
            if (d < 0) d = -d;
            if (d > 32767) d = 32767;
            ioState.lastSlopeAbsY = static_cast<int16_t>(d);

            const int32_t wheelbaseRaw =
                Tunables::kProbeHalfWheelBase.RawValue() << 1;
            int32_t sampleTan = 0;
            if (wheelbaseRaw > 0)
            {
                sampleTan = static_cast<int32_t>(
                    (static_cast<int64_t>(rawChordForGrade) << 16) /
                    static_cast<int64_t>(wheelbaseRaw));
            }
            const int32_t tanMax = Tunables::kSlopeTanMax.RawValue();
            if (sampleTan > tanMax) sampleTan = tanMax;
            if (sampleTan < -tanMax) sampleTan = -tanMax;

            const int32_t tanAbs = (sampleTan < 0) ? -sampleTan : sampleTan;
            auto filterGrade = [&](int32_t sample, int32_t& tanRaw, bool& valid)
            {
                if (!valid)
                {
                    tanRaw = sample;
                    valid = true;
                }
                else
                {
                    tanRaw += (sample - tanRaw) >> Tunables::kGradeFilterShift;
                }
            };
            auto decayGrade = [&](int32_t& tanRaw, bool& valid, int shift)
            {
                tanRaw -= (tanRaw >> shift);
                if (tanRaw < (1 << 10) && tanRaw > -(1 << 10))
                {
                    tanRaw = 0;
                    valid = false;
                }
            };

            // Freeze grade while axles split — do not learn embicada tan.
            if (axleJunction)
            {
                // Keep last continuous tan for heave gradeDy + pitch hold.
                if (ioState.gradeValid)
                {
                    ioState.gradeHoldFrames = Tunables::kGradeHoldMaxFrames;
                }
                if (ioState.gradeAttitudeValid)
                {
                    ioState.gradeAttitudeHoldFrames =
                        Tunables::kGradeAttitudeHoldMaxFrames;
                }
            }
            else if (tanAbs >= Tunables::kSlopeTanDeadzone.RawValue())
            {
                filterGrade(sampleTan, ioState.gradeTanRaw, ioState.gradeValid);
                filterGrade(sampleTan, ioState.gradeTanAttitudeRaw,
                            ioState.gradeAttitudeValid);
                if constexpr (Tunables::kEnableContinuousGradeSlide)
                {
                    ioState.gradeHoldFrames = Tunables::kGradeHoldMaxFrames;
                }
                ioState.gradeAttitudeHoldFrames =
                    Tunables::kGradeAttitudeHoldMaxFrames;
            }
            else
            {
                // F−R collapsed on slab: heave may hold long; attitude decays fast.
                // Near stop: do not burn hold frames (resume without re-stair).
                if constexpr (Tunables::kEnableContinuousGradeSlide)
                {
                    const int32_t spd = (ioState.lastForwardSpeedRaw < 0)
                        ? -ioState.lastForwardSpeedRaw
                        : ioState.lastForwardSpeedRaw;
                    const bool nearlyStopped =
                        spd < Tunables::kGradeHoldMinSpeed.RawValue();
                    if (ioState.gradeHoldFrames > 0u)
                    {
                        if (!nearlyStopped)
                        {
                            --ioState.gradeHoldFrames;
                        }
                        ioState.gradeValid = true;
                    }
                    else if (ioState.gradeValid && !nearlyStopped)
                    {
                        decayGrade(ioState.gradeTanRaw, ioState.gradeValid, 2);
                    }
                }
                else if (ioState.gradeValid)
                {
                    filterGrade(sampleTan, ioState.gradeTanRaw, ioState.gradeValid);
                }

                // Flat F−R on a slab: keep attitude aligned to heave grade so
                // pitch/cam follow the continuous plane (not zero mid-segment).
                if (ioState.gradeValid &&
                    (ioState.gradeTanRaw > Tunables::kTopoGradeDeclineMin.RawValue() ||
                     ioState.gradeTanRaw < -Tunables::kTopoGradeDeclineMin.RawValue()))
                {
                    ioState.gradeTanAttitudeRaw +=
                        (ioState.gradeTanRaw - ioState.gradeTanAttitudeRaw) >> 1;
                    ioState.gradeAttitudeValid = true;
                    ioState.gradeAttitudeHoldFrames =
                        Tunables::kGradeAttitudeHoldMaxFrames;
                }
                else if (ioState.gradeAttitudeHoldFrames > 0u)
                {
                    --ioState.gradeAttitudeHoldFrames;
                    ioState.gradeTanAttitudeRaw -=
                        (ioState.gradeTanAttitudeRaw >> 2);
                    ioState.gradeAttitudeValid =
                        (ioState.gradeTanAttitudeRaw > (1 << 10) ||
                         ioState.gradeTanAttitudeRaw < -(1 << 10));
                }
                else if (ioState.gradeAttitudeValid)
                {
                    decayGrade(ioState.gradeTanAttitudeRaw,
                               ioState.gradeAttitudeValid, 1);
                }
            }
        }
        else
        {
            // No F−R pair this frame.
            if constexpr (Tunables::kEnableContinuousGradeSlide)
            {
                const int32_t spd = (ioState.lastForwardSpeedRaw < 0)
                    ? -ioState.lastForwardSpeedRaw
                    : ioState.lastForwardSpeedRaw;
                const bool nearlyStopped =
                    spd < Tunables::kGradeHoldMinSpeed.RawValue();
                if (ioState.gradeHoldFrames > 0u)
                {
                    if (!nearlyStopped)
                    {
                        --ioState.gradeHoldFrames;
                    }
                }
                else if (ioState.gradeValid && !nearlyStopped)
                {
                    ioState.gradeTanRaw -= (ioState.gradeTanRaw >> 2);
                    if (ioState.gradeTanRaw < (1 << 10) &&
                        ioState.gradeTanRaw > -(1 << 10))
                    {
                        ioState.gradeTanRaw = 0;
                        ioState.gradeValid = false;
                    }
                }
            }
            else if (ioState.gradeValid)
            {
                ioState.gradeTanRaw -= (ioState.gradeTanRaw >> 2);
                if (ioState.gradeTanRaw < (1 << 10) &&
                    ioState.gradeTanRaw > -(1 << 10))
                {
                    ioState.gradeTanRaw = 0;
                    ioState.gradeValid = false;
                }
            }

            if (ioState.gradeValid &&
                (ioState.gradeTanRaw > Tunables::kTopoGradeDeclineMin.RawValue() ||
                 ioState.gradeTanRaw < -Tunables::kTopoGradeDeclineMin.RawValue()))
            {
                ioState.gradeTanAttitudeRaw +=
                    (ioState.gradeTanRaw - ioState.gradeTanAttitudeRaw) >> 1;
                ioState.gradeAttitudeValid = true;
                ioState.gradeAttitudeHoldFrames =
                    Tunables::kGradeAttitudeHoldMaxFrames;
            }
            else if (ioState.gradeAttitudeHoldFrames > 0u)
            {
                --ioState.gradeAttitudeHoldFrames;
                ioState.gradeTanAttitudeRaw -=
                    (ioState.gradeTanAttitudeRaw >> 2);
                ioState.gradeAttitudeValid =
                    (ioState.gradeTanAttitudeRaw > (1 << 10) ||
                     ioState.gradeTanAttitudeRaw < -(1 << 10));
            }
            else if (ioState.gradeAttitudeValid)
            {
                ioState.gradeTanAttitudeRaw -=
                    (ioState.gradeTanAttitudeRaw >> 1);
                if (ioState.gradeTanAttitudeRaw < (1 << 10) &&
                    ioState.gradeTanAttitudeRaw > -(1 << 10))
                {
                    ioState.gradeTanAttitudeRaw = 0;
                    ioState.gradeAttitudeValid = false;
                }
            }
        }

        if (!ioState.hasGroundSupport)
        {
            ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYTarget);
            ioFrameState.debugGroundYBody = FxpToDebugInt(ioState.surfaceYFiltered);
            return;
        }

        // Measured MapHeight plane + continuous grade slide across slabs/junctions.
        int32_t targetPlaneCenterYRaw = 0;
        if (!ArcadeSuspensionFilter::AverageTargetYRaw(
                ioState.suspension, targetPlaneCenterYRaw))
        {
            ioState.hasGroundSupport = ioState.surfaceYFilterInitialized;
            ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYTarget);
            ioFrameState.debugGroundYBody = FxpToDebugInt(ioState.surfaceYFiltered);
            return;
        }

        // Heave base: face = raw 4-wheel + ride offset (16-17 plant on asphalt).
        const int32_t rawMeasuredRide =
            targetPlaneCenterYRaw + GetRideHeightOffset().RawValue();
        // Prefer face; continuous only for telemetry / look-ahead seed.
        int32_t measuredRideRaw = rawMeasuredRide;
        const bool junctionHeaveNow = axleSplitNow;
        if (ioState.surfaceYInitialized && ioState.gradeValid)
        {
            int32_t cont = ioState.surfaceYTarget.RawValue();
            const int64_t dy64 =
                (static_cast<int64_t>(ioState.gradeTanRaw) *
                 static_cast<int64_t>(ioState.lastForwardSpeedRaw)) >> 16;
            int32_t g = static_cast<int32_t>(std::clamp<int64_t>(
                dy64,
                -static_cast<int64_t>(Tunables::kMaxBodySlideUpY.RawValue()),
                static_cast<int64_t>(Tunables::kMaxBodySlideDownY.RawValue())));
            cont += g;
            // 3/4 face + 1/4 continuous (was 1/2 — left car high on decline).
            measuredRideRaw = cont + (((rawMeasuredRide - cont) * 3) >> 2);
        }
        (void)junctionHeaveNow;
        Fxp rideTarget = Fxp::BuildRaw(measuredRideRaw);

        // Speed-aware seam limit: crawl uses tight rate-limit so MapHeight
        // segment steps become ramps (AAA stair damp / ground-hug pattern).
        const int32_t speedAbs = (ioState.lastForwardSpeedRaw < 0)
            ? -ioState.lastForwardSpeedRaw
            : ioState.lastForwardSpeedRaw;
        const bool lowSpeedHeave =
            speedAbs < Tunables::kLowSpeedForHeaveSmooth.RawValue();

        if constexpr (Tunables::kEnableContinuousGradeSlide)
        {
            // --- Look-ahead MapHeight (front + seed+1) -----------------------------
            int32_t aheadRideRaw = measuredRideRaw;
            bool aheadValid = false;
            {
                const Fxp lookLong = Fxp::BuildRaw(
                    Tunables::kProbeHalfWheelBase.RawValue() +
                    (Tunables::kProbeHalfWheelBase.RawValue() >> 1));
                SurfaceProbeSample ahead{};
                const int32_t lookSeed =
                    (seedSegmentId > 0) ? seedSegmentId : -1;
                if (TryProbeSurfaceY(
                        trackQuery,
                        BuildProbePoint(worldPosition, sinYaw, cosYaw, lookLong,
                                        Fxp::BuildRaw(0)),
                        lookSeed,
                        ahead,
                        !Tunables::kEnableWheelStrictSurface,
                        -1) &&
                    ahead.valid)
                {
                    aheadValid = true;
                    aheadRideRaw = ahead.y.RawValue() + GetRideHeightOffset().RawValue();
                }
                else if (lookSeed > 0 &&
                         TryProbeSurfaceY(
                             trackQuery,
                             BuildProbePoint(worldPosition, sinYaw, cosYaw, lookLong,
                                             Fxp::BuildRaw(0)),
                             lookSeed + 1,
                             ahead,
                             !Tunables::kEnableWheelStrictSurface,
                             -1) &&
                         ahead.valid)
                {
                    aheadValid = true;
                    aheadRideRaw = ahead.y.RawValue() + GetRideHeightOffset().RawValue();
                }
            }

            // --- LPF MapHeight: convert segment steps into a continuous ramp -------
            // Large measured drops (new segment) must not reach body as a dive.
            // Nearly stopped: plant on measured (video 14-09-09 sink/float).
            int32_t planeY = measuredRideRaw;
            const bool nearlyStoppedPlane =
                speedAbs < Tunables::kGradeHoldMinSpeed.RawValue();
            if (!ioState.smoothedRideYValid)
            {
                ioState.smoothedRideYRaw = measuredRideRaw;
                ioState.smoothedRideYValid = true;
            }
            else if (nearlyStoppedPlane)
            {
                // Half toward measured every frame — settle onto asphalt.
                int32_t d = measuredRideRaw - ioState.smoothedRideYRaw;
                int32_t step = d >> 1;
                if (step == 0 && d != 0)
                {
                    step = (d > 0) ? 1 : -1;
                }
                ioState.smoothedRideYRaw += step;
                const int32_t err =
                    measuredRideRaw - ioState.smoothedRideYRaw;
                if (err <= Tunables::kHeaveSnapEpsY.RawValue() &&
                    err >= -Tunables::kHeaveSnapEpsY.RawValue())
                {
                    ioState.smoothedRideYRaw = measuredRideRaw;
                }
            }
            else
            {
                int32_t d = measuredRideRaw - ioState.smoothedRideYRaw;
                const int32_t jumpAbs = (d < 0) ? -d : d;
                // Seam dive: measured jumped more than a normal frame of grade.
                const bool seamJump =
                    jumpAbs > Tunables::kRideSeamJumpY.RawValue() || axleJunction;
                int32_t maxStep = Tunables::kMaxRideTargetStepY.RawValue();
                if (lowSpeedHeave || seamJump)
                {
                    maxStep = Tunables::kMaxRideTargetStepLowSpeedY.RawValue();
                }
                // Downward seams (d>0, Y-down deeper): always use tight step.
                if (d > 0 && seamJump)
                {
                    maxStep = Tunables::kMaxRideTargetStepLowSpeedY.RawValue();
                }
                // While floating (measured deeper than smooth): allow faster
                // catch-up than seam crawl so tires don't hang in the air.
                if (d > Tunables::kFloatCatchupY.RawValue() && !seamJump)
                {
                    maxStep = Tunables::kMaxRideTargetStepY.RawValue();
                }
                if (d > maxStep) d = maxStep;
                if (d < -maxStep) d = -maxStep;
                // Always half-step after rate-limit — spreads the remaining stair.
                int32_t step = d >> 1;
                if (step == 0 && d != 0)
                {
                    step = (d > 0) ? 1 : -1;
                }
                ioState.smoothedRideYRaw += step;
            }
            planeY = ioState.smoothedRideYRaw;

            // gradeDy = tanθ · speed (zero when stopped — LPF handles seams).
            int32_t gradeDy = 0;
            if (ioState.gradeValid)
            {
                const int64_t dy64 =
                    (static_cast<int64_t>(ioState.gradeTanRaw) *
                     static_cast<int64_t>(ioState.lastForwardSpeedRaw)) >> 16;
                gradeDy = static_cast<int32_t>(std::clamp<int64_t>(
                    dy64,
                    -static_cast<int64_t>(Tunables::kMaxYStepUpPerFrame.RawValue()),
                    static_cast<int64_t>(Tunables::kMaxYStepDownPerFrame.RawValue())));
            }

            // Look-ahead: continuous dy + soft grade pull. Never during axle split.
            const bool junctionHeave = axleSplitNow;
            if (aheadValid && !junctionHeave)
            {
                const int32_t lookDist =
                    Tunables::kProbeHalfWheelBase.RawValue() +
                    (Tunables::kProbeHalfWheelBase.RawValue() >> 1);
                if (lookDist > 0)
                {
                    // Continuous look dy at ANY speed (critical at crawl: gradeDy≈0).
                    if (aheadRideRaw != measuredRideRaw && speedAbs > 0)
                    {
                        const int64_t gap =
                            static_cast<int64_t>(aheadRideRaw - measuredRideRaw);
                        const int64_t step64 =
                            (gap * static_cast<int64_t>(speedAbs)) /
                            static_cast<int64_t>(lookDist);
                        int32_t lookDy = static_cast<int32_t>(std::clamp<int64_t>(
                            step64,
                            -static_cast<int64_t>(
                                Tunables::kMaxYStepUpPerFrame.RawValue()),
                            static_cast<int64_t>(
                                Tunables::kMaxYStepDownPerFrame.RawValue())));
                        // Prefer larger |dy| that matches continuous descent.
                        if (lookDy > gradeDy) gradeDy = lookDy;
                    }
                    // Ease plane toward deeper ahead (1/8).
                    if (aheadRideRaw > planeY)
                    {
                        planeY += (aheadRideRaw - planeY) >> 3;
                    }
                    const int32_t lookTan = static_cast<int32_t>(
                        (static_cast<int64_t>(aheadRideRaw - measuredRideRaw) << 16) /
                        static_cast<int64_t>(lookDist));
                    const int32_t lookAbs =
                        (lookTan < 0) ? -lookTan : lookTan;
                    if (lookAbs > Tunables::kTopoGradeDeclineMin.RawValue())
                    {
                        // Soft pull only — never hard replace.
                        if (!ioState.gradeValid)
                        {
                            ioState.gradeTanRaw = lookTan;
                        }
                        else
                        {
                            ioState.gradeTanRaw +=
                                (lookTan - ioState.gradeTanRaw) >> 3;
                        }
                        ioState.gradeValid = true;
                        ioState.gradeHoldFrames = Tunables::kGradeHoldMaxFrames;
                        ioState.gradeTanAttitudeRaw +=
                            (lookTan - ioState.gradeTanAttitudeRaw) >> 3;
                        ioState.gradeAttitudeValid = true;
                        if (ioState.gradeAttitudeHoldFrames < 4u)
                        {
                            ioState.gradeAttitudeHoldFrames = 4u;
                        }
                    }
                }
            }

            // Height plant (16-17): face MapHeight is truth — plant ON asphalt.
            // 16-17 float: continuous lag + large rideOffset left car high.
            // Prefer face; continuous only softens seams, never keeps air gap.
            const int32_t maxAir = Tunables::kMaxAirAboveMeasuredY.RawValue();
            const int32_t maxPen = Tunables::kMaxPenetrateMeasuredY.RawValue();
            const int32_t maxSlide = Tunables::kMaxBodySlideDownY.RawValue();
            const int32_t maxUpStep = Tunables::kMaxBodySlideUpY.RawValue();
            const bool highSpeedPlant =
                speedAbs >= Tunables::kHighSpeedPlantGlue.RawValue();

            // Face = raw 4-wheel avg + ride offset.
            const int32_t faceRide = rawMeasuredRide;

            int32_t slid = faceRide;
            if (ioState.surfaceYInitialized)
            {
                const int32_t prev = ioState.surfaceYTarget.RawValue();

                // Continuous candidate (smooth decline).
                int32_t cont = prev;
                {
                    int32_t g = gradeDy;
                    if (g > maxSlide) g = maxSlide;
                    if (g < -maxUpStep) g = -maxUpStep;
                    cont = prev + g;
                }

                // Default target = face (plant). Soften only large seams via cont.
                int32_t target = faceRide;
                const int32_t seam = faceRide - prev;
                const int32_t seamAbs = (seam < 0) ? -seam : seam;
                if (seamAbs > Tunables::kRideSeamJumpY.RawValue())
                {
                    // Big MapHeight jump: blend cont + face so stairs soften.
                    const int32_t glue = faceRide - cont;
                    if (junctionHeave)
                    {
                        target = cont + (glue >> 1);
                    }
                    else if (highSpeedPlant)
                    {
                        target = cont + ((glue * 3) >> 2);
                    }
                    else
                    {
                        target = cont + (glue >> 1);
                    }
                }

                int32_t d = target - prev;
                // Y-down: floating = prev < face (body shallower than asphalt).
                const int32_t air = faceRide - prev;
                if (air > maxAir)
                {
                    // Plant down hard — 3/4 residual toward face (16-17 too high).
                    int32_t drop = air;
                    drop = (drop * 3) >> 2;
                    if (drop == 0) drop = 1;
                    if (drop > maxSlide) drop = maxSlide;
                    d = drop;
                }
                else if (prev - faceRide > maxPen)
                {
                    // Buried: climb half residual.
                    int32_t lift = faceRide - prev;
                    lift >>= 1;
                    if (lift == 0) lift = -1;
                    if (lift < -maxUpStep) lift = -maxUpStep;
                    d = lift;
                }
                else
                {
                    int32_t maxDown = junctionHeave
                        ? Tunables::kJunctionMaxHeaveDownY.RawValue()
                        : maxSlide;
                    if (d > maxDown) d = maxDown;
                    if (d < -maxUpStep) d = -maxUpStep;
                }
                slid = prev + d;
            }
            (void)planeY;
            (void)aheadValid;
            (void)aheadRideRaw;
            (void)measuredRideRaw;
            (void)lowSpeedHeave;

            // ALWAYS clamp to face band (almost zero air).
            if (slid < faceRide - maxAir)
            {
                slid = faceRide - maxAir;
            }
            if (slid > faceRide + maxPen)
            {
                slid = faceRide + maxPen;
            }

            rideTarget = Fxp::BuildRaw(slid);
            ioState.lastMeasuredRideYRaw = faceRide;
            ioState.lastMeasuredRideYValid = true;
            ioState.committedSurfaceYRaw = rideTarget.RawValue();
            ioState.committedSurfaceYValid = true;
            ioState.surfaceTargetVelocityRaw = gradeDy;
        }
        else
        {
            (void)useReducedProbe;
            // Still LPF measured when continuous slide is off.
            if (!ioState.smoothedRideYValid)
            {
                ioState.smoothedRideYRaw = measuredRideRaw;
                ioState.smoothedRideYValid = true;
            }
            else
            {
                int32_t d = measuredRideRaw - ioState.smoothedRideYRaw;
                const int32_t maxStep = lowSpeedHeave
                    ? Tunables::kMaxRideTargetStepLowSpeedY.RawValue()
                    : Tunables::kMaxRideTargetStepY.RawValue();
                if (d > maxStep) d = maxStep;
                if (d < -maxStep) d = -maxStep;
                ioState.smoothedRideYRaw += (d >> 1);
            }
            rideTarget = Fxp::BuildRaw(ioState.smoothedRideYRaw);
        }

        // No nose clearance lift — video 14-09-09: any chord-based lift left
        // visible air under tires on the decline (F1 PS1 stays glued).

        if (ioState.surfaceYFilterInitialized)
        {
            const int32_t cur = ioState.surfaceYFiltered.RawValue();
            const int32_t tgt = rideTarget.RawValue();
            // Per-frame topology under the car (not only segment seams).
            bool drop = false;
            // Body above new surface this frame.
            if (tgt > cur + Tunables::kTopoDropYThreshold.RawValue())
            {
                drop = true;
            }
            // Surface fell vs last accepted height.
            if (ioState.lastAcceptedSurfaceYValid &&
                tgt > ioState.lastAcceptedSurfaceYRaw + Tunables::kTopoDropYThreshold.RawValue())
            {
                drop = true;
            }
            // Live decline chord under axles (use attitude-capped publish).
            if (frontValid && rearValid)
            {
                const int32_t chord = pubFrontRaw - pubRearRaw;
                if (chord > Tunables::kTopoGradeDeclineMin.RawValue())
                {
                    drop = true;
                }
            }
            if (drop)
            {
                ioState.topologyDropFrames = Tunables::kTopoDropHoldFrames;
            }
        }
        else
        {
            // First init on a decline: still mark topology so adhesion sticks.
            if (frontValid && rearValid &&
                (frontY.RawValue() - rearY.RawValue()) >
                    Tunables::kTopoGradeDeclineMin.RawValue())
            {
                ioState.topologyDropFrames = Tunables::kTopoDropHoldFrames;
            }
        }
        ioState.surfaceYTarget = rideTarget;
        ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYTarget);
        ioFrameState.debugGroundYBody = FxpToDebugInt(ioState.surfaceYFiltered);
        {
            const int32_t dy =
                (ioState.surfaceYTarget.RawValue() - ioState.surfaceYFiltered.RawValue()) >> 16;
            ioFrameState.debugGroundDY =
                static_cast<int16_t>(std::clamp<int32_t>(dy, -32768, 32767));
        }
        ioFrameState.debugTopoDrop = (ioState.topologyDropFrames > 0u) ? 1u : 0u;
        // Camera/body use attitude grade (short hold), not heave hold.
        if (ioState.gradeAttitudeValid)
        {
            const int32_t g100 = static_cast<int32_t>(
                (static_cast<int64_t>(ioState.gradeTanAttitudeRaw) * 100) >> 16);
            ioFrameState.debugGradeTanX100 =
                static_cast<int16_t>(std::clamp<int32_t>(g100, -32768, 32767));
        }
        else
        {
            ioFrameState.debugGradeTanX100 = 0;
        }
        ioState.surfaceYInitialized = true;

        const int32_t targetSegmentId =
            ResolveSegmentId(fl, fr, rl, rr,
                             (sampledSegmentId > 0) ? sampledSegmentId : static_cast<int32_t>(ioState.lastSurfaceSegmentId));
        const int32_t previousSurfaceSegmentId = static_cast<int32_t>(ioState.lastSurfaceSegmentId);
        if (targetSegmentId > 0)
        {
            ioState.lastSurfaceSegmentId = static_cast<int16_t>(targetSegmentId);
        }

        if (trackQuery)
        {
            const uint8_t contactCadence = Tunables::kSurfaceContactCadenceFrames;
            if (ioState.surfaceContactCooldown > 0u)
            {
                --ioState.surfaceContactCooldown;
            }
            const bool segmentChanged =
                (targetSegmentId > 0) &&
                (targetSegmentId != previousSurfaceSegmentId);
            const bool cadenceExpired =
                (contactCadence <= 1u) ||
                (ioState.surfaceContactCooldown == 0u);
            const bool shouldRefreshContact =
                segmentChanged ||
                (ioState.lastSurfaceFaceIndex < 0) ||
                cadenceExpired;

            if (shouldRefreshContact)
            {
                Game::SurfaceContact contact{};
                if (trackQuery->SampleSurfaceContact(worldPosition, contact, targetSegmentId) &&
                    contact.valid)
                {
                    ioState.lastSurfaceFaceIndex = contact.faceIndex;
                    ioState.lastSurfaceFamilyId = contact.familyId;
                    ioState.lastSurfaceType = contact.surfaceType;
                    ioFrameState.groundFaceIndex = contact.faceIndex;
                    ioFrameState.groundFamilyId = contact.familyId;
                    ioFrameState.groundSurfaceType = contact.surfaceType;
                }
                else
                {
                    ioFrameState.groundFaceIndex = ioState.lastSurfaceFaceIndex;
                    ioFrameState.groundFamilyId = ioState.lastSurfaceFamilyId;
                    ioFrameState.groundSurfaceType = ioState.lastSurfaceType;
                }
                ioState.surfaceContactCooldown =
                    (contactCadence > 0u)
                        ? static_cast<uint8_t>(contactCadence - 1u)
                        : 0u;
            }
            else
            {
                ioFrameState.groundFaceIndex = ioState.lastSurfaceFaceIndex;
                ioFrameState.groundFamilyId = ioState.lastSurfaceFamilyId;
                ioFrameState.groundSurfaceType = ioState.lastSurfaceType;
            }
        }
        else
        {
            ioFrameState.groundFaceIndex = ioState.lastSurfaceFaceIndex;
            ioFrameState.groundFamilyId = ioState.lastSurfaceFamilyId;
            ioFrameState.groundSurfaceType = ioState.lastSurfaceType;
        }

        const Fxp rightX = cosYaw;
        const Fxp rightZ = sinYaw;
        ioState.edgeLeftLost = !leftValid && rightValid;
        ioState.edgeRightLost = !rightValid && leftValid;
        if (ioState.edgeLeftLost)
        {
            ioState.correctionX += rightX * Tunables::kEdgeRecoverPush;
            ioState.correctionZ += rightZ * Tunables::kEdgeRecoverPush;
        }
        else if (ioState.edgeRightLost)
        {
            ioState.correctionX -= rightX * Tunables::kEdgeRecoverPush;
            ioState.correctionZ -= rightZ * Tunables::kEdgeRecoverPush;
        }

        const bool shouldQueryWalls = Tunables::kEnableWallPlanarPush;
        if (shouldQueryWalls)
        {
            const int32_t frameId = static_cast<int32_t>(ioFrameState.frameId);
            const int32_t wallFrameDelta =
                (ioState.lastWallQueryFrameId < 0)
                    ? 0
                    : (frameId - ioState.lastWallQueryFrameId);
            const bool shouldRefreshWallQuery =
                (ioState.lastWallQueryFrameId < 0) ||
                (wallFrameDelta >= 1);
            if (shouldRefreshWallQuery)
            {
                Vector3D wallPush{};
                Fxp wallRadius = Tunables::kWallCollisionRadius;
                if (ioFrameState.speedProxy > 0)
                {
                    const int32_t speedProxyClamped =
                        std::clamp<int32_t>(ioFrameState.speedProxy, 0, Tunables::kTargetTopSpeedKmh);
                    // Extra lookahead radius: up to +0.75 at top speed.
                    const Fxp dynamicExtra =
                        Fxp::BuildRaw((speedProxyClamped << 16) / 400);
                    wallRadius += dynamicExtra;
                }
                ioState.lastWallQueryHit = ResolveWallPushMultiProbe(trackQuery,
                                                                     worldPosition,
                                                                     sinYaw,
                                                                     cosYaw,
                                                                     wallRadius,
                                                                     targetSegmentId,
                                                                     wallPush,
                                                                     &ioState.lastWallQuerySegmentId);
                ioState.lastWallQueryFrameId = frameId;
                ioState.lastWallPushX = ioState.lastWallQueryHit ? wallPush.X : Fxp::BuildRaw(0);
                ioState.lastWallPushZ = ioState.lastWallQueryHit ? wallPush.Z : Fxp::BuildRaw(0);
            }

            if (ioState.lastWallQueryHit)
            {
                ioFrameState.debugWallHit = 1u;
                ioFrameState.debugWallSegmentId = ioState.lastWallQuerySegmentId;
                ioFrameState.debugWallPushX = FxpToDebugInt(ioState.lastWallPushX);
                ioFrameState.debugWallPushZ = FxpToDebugInt(ioState.lastWallPushZ);
                if (ioState.lastWallApplyFrameId != frameId)
                {
                    ioState.correctionX += ioState.lastWallPushX;
                    ioState.correctionZ += ioState.lastWallPushZ;
                    ioState.lastWallApplyFrameId = frameId;
                }
            }
        }

        // Clamp planar correction so adhesion/collision does not cancel steering
        // and produce orbit-like camera behavior around an almost static car.
        ioState.correctionX = Clamp(ioState.correctionX,
                                    Fxp::BuildRaw(-Tunables::kMaxPlanarCorrectionPerFrame.RawValue()),
                                    Tunables::kMaxPlanarCorrectionPerFrame);
        ioState.correctionZ = Clamp(ioState.correctionZ,
                                    Fxp::BuildRaw(-Tunables::kMaxPlanarCorrectionPerFrame.RawValue()),
                                    Tunables::kMaxPlanarCorrectionPerFrame);
        ioFrameState.debugCorrX = FxpToDebugInt(ioState.correctionX);
        ioFrameState.debugCorrZ = FxpToDebugInt(ioState.correctionZ);

    }
};
} // namespace Game::CarPhysics
