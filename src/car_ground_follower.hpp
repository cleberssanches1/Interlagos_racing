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

    // After planar XZ integrate: nudge Y along last-frame grade so the next
    // surface probe samples on the ramp (aligned path, not flat + vertical glue).
    static void PredictYAlongGrade(GroundState& ioState,
                                   const Fxp& forwardSpeed,
                                   Vector3D& ioCarWorldPosition)
    {
        if constexpr (!Tunables::kEnableGradePredictY)
        {
            return;
        }
        if (!ioState.gradeValid || !ioState.surfaceYFilterInitialized)
        {
            return;
        }

        // dy ≈ tanθ · s_forward (body longitudinal). Y-down: +tan · +speed ⇒ +Y (downhill).
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

        // Wheel targets already reject face/seam chatter. Feed their coherent
        // motion forward so the chassis follows a long ramp without floating;
        // only residual error is damped and rate-limited here.
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
            const int32_t previousTarget = ioState.lastAcceptedSurfaceYValid
                ? ioState.lastAcceptedSurfaceYRaw
                : tgt;
            const int32_t next = ArcadeSuspensionFilter::StepChassisTracking(
                tgt,
                cur,
                previousTarget,
                velocity,
                Tunables::kMaxYStepUpPerFrame.RawValue(),
                Tunables::kMaxYStepDownPerFrame.RawValue());
            ioState.verticalVelocity = Fxp::BuildRaw(velocity);
            ioState.surfaceYFiltered = Fxp::BuildRaw(next);
        }
        if (ioState.topologyDropFrames > 0u)
        {
            --ioState.topologyDropFrames;
        }
        ioCarWorldPosition.Y = ioState.surfaceYFiltered;
        ioState.surfaceContactFrames = 8u;

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
        const bool stagingFirstDiagonal =
            useReducedProbe && ((ioState.suspension.diagonalPhase & 1u) == 0u);
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
            (void)TryProbeSurfaceY(trackQuery,
                                   BuildProbePoint(worldPosition,
                                                   sinYaw,
                                                   cosYaw,
                                                   longitudinal,
                                                   lateral),
                                   wheelSeedSegmentId,
                                   sample,
                                   !Tunables::kEnableWheelStrictSurface,
                                   wheelHintFaceIndex);
            if (sample.valid)
            {
                const bool hadTarget =
                    (ioState.suspension.validMask & wheelBit) != 0u;
                const int32_t previousTargetYRaw =
                    ioState.suspension.targetYRaw[wheelIndex];
                const bool accepted = ArcadeSuspensionFilter::Observe(
                    ioState.suspension,
                    wheelIndex,
                    sample.y.RawValue(),
                    sample.segmentId,
                    sample.faceIndex);
                if (accepted && hadTarget && stagingFirstDiagonal)
                {
                    ArcadeSuspensionFilter::DeferObservedTarget(
                        ioState.suspension,
                        wheelIndex,
                        previousTargetYRaw);
                }
            }
        };
        if (useReducedProbe)
        {
            // Preserve two queries per frame, alternating wheel diagonals.
            // Every corner owns a persistent spring, so all four filtered
            // contacts advance even when only two targets are refreshed.
            if ((ioState.suspension.diagonalPhase & 1u) == 0u)
            {
                sampleWheel(0u, longFront, latLeft);
                sampleWheel(3u, longRear, latRight);
            }
            else
            {
                sampleWheel(1u, longFront, latRight);
                sampleWheel(2u, longRear, latLeft);
                ArcadeSuspensionFilter::CommitDeferredTargets(ioState.suspension);
            }
            ioState.suspension.diagonalPhase ^= 1u;
        }
        else
        {
            sampleWheel(0u, longFront, latLeft);
            sampleWheel(1u, longFront, latRight);
            sampleWheel(2u, longRear, latLeft);
            sampleWheel(3u, longRear, latRight);
        }
        const bool completedContactCycle =
            !useReducedProbe ||
            ((ioState.suspension.diagonalPhase & 1u) == 0u);

        ArcadeSuspensionFilter::StepWheels(ioState.suspension);
        auto restoreWheel = [&](uint8_t wheelIndex, SurfaceProbeSample& sample)
        {
            int32_t yRaw = 0;
            int32_t segmentId = -1;
            int32_t faceIndex = -1;
            sample.valid = ArcadeSuspensionFilter::Read(ioState.suspension,
                                                        wheelIndex,
                                                        yRaw,
                                                        segmentId,
                                                        &faceIndex);
            if (!sample.valid) return;
            sample.y = Fxp::BuildRaw(yRaw);
            sample.segmentId = segmentId;
            sample.faceIndex = static_cast<int16_t>(faceIndex);
        };
        const bool completeWheelSnapshot =
            !useReducedProbe || ((ioState.suspension.validMask & 0x0Fu) == 0x0Fu);
        if (completeWheelSnapshot)
        {
            restoreWheel(0u, fl);
            restoreWheel(1u, fr);
            restoreWheel(2u, rl);
            restoreWheel(3u, rr);
        }

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

        // Both axles must retain support before accepting a new body target.
        // Cached corners bridge one-frame diagonal alternation and brief seam misses.
        if (!frontValid || !rearValid)
        {
            ioState.hasGroundSupport = false;
            ioState.surfaceYInitialized = false;
            ioState.committedSurfaceYValid = false;
            ioState.surfaceTargetVelocityRaw = 0;
            ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYFiltered);
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

        // Chassis attitude consumes only filtered wheel contacts. Raw face
        // samples never bypass the per-corner suspension state.
        ioFrameState.debugGroundYFront = frontValid ? FxpToDebugInt(frontY) : 0;
        ioFrameState.debugGroundYRear = rearValid ? FxpToDebugInt(rearY) : 0;
        ioFrameState.debugGroundYLeft = leftValid ? FxpToDebugInt(leftY) : 0;
        ioFrameState.debugGroundYRight = rightValid ? FxpToDebugInt(rightY) : 0;
        ioFrameState.debugGroundYFrontRaw = frontValid ? frontY.RawValue() : 0;
        ioFrameState.debugGroundYRearRaw = rearValid ? rearY.RawValue() : 0;
        ioFrameState.debugGroundYLeftRaw = leftValid ? leftY.RawValue() : 0;
        ioFrameState.debugGroundYRightRaw = rightValid ? rightY.RawValue() : 0;

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
            int32_t d = static_cast<int32_t>(
                (frontY.RawValue() - rearY.RawValue()) >> 16);
            if (d < 0) d = -d;
            if (d > 32767) d = 32767;
            ioState.lastSlopeAbsY = static_cast<int16_t>(d);

            const int32_t wheelbaseRaw =
                Tunables::kProbeHalfWheelBase.RawValue() << 1;
            int32_t sampleTan = 0;
            if (wheelbaseRaw > 0)
            {
                sampleTan = static_cast<int32_t>(
                    (static_cast<int64_t>(frontY.RawValue() - rearY.RawValue()) << 16) /
                    static_cast<int64_t>(wheelbaseRaw));
            }
            const int32_t tanMax = Tunables::kSlopeTanMax.RawValue();
            if (sampleTan > tanMax) sampleTan = tanMax;
            if (sampleTan < -tanMax) sampleTan = -tanMax;

            if (!ioState.gradeValid)
            {
                ioState.gradeTanRaw = sampleTan;
                ioState.gradeValid = true;
            }
            else
            {
                const int32_t delta = sampleTan - ioState.gradeTanRaw;
                ioState.gradeTanRaw += (delta >> Tunables::kGradeFilterShift);
            }
        }
        else if (ioState.gradeValid)
        {
            ioState.gradeTanRaw -= (ioState.gradeTanRaw >> 2);
            if (ioState.gradeTanRaw < (1 << 10) && ioState.gradeTanRaw > -(1 << 10))
            {
                ioState.gradeTanRaw = 0;
                ioState.gradeValid = false;
            }
        }

        if (!ioState.hasGroundSupport)
        {
            ioState.surfaceYInitialized = false;
            ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYFiltered);
            return;
        }

        // Separate heave from attitude. Filtered wheels still drive pitch/roll;
        // the validated target plane drives common chassis height without the
        // permanent spring lag that made the whole car float over a long ramp.
        int32_t targetPlaneCenterYRaw = 0;
        if (!ArcadeSuspensionFilter::AverageTargetYRaw(
                ioState.suspension, targetPlaneCenterYRaw))
        {
            ioState.surfaceYInitialized = false;
            ioState.committedSurfaceYValid = false;
            ioState.surfaceTargetVelocityRaw = 0;
            ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYFiltered);
            return;
        }

        const Fxp blendedY = Fxp::BuildRaw(targetPlaneCenterYRaw);
        Fxp rideTarget = blendedY + GetRideHeightOffset();
        // A complete low-cost contact plane arrives every two frames. Convert
        // those 30 Hz measurements into a continuous 60 Hz target: commit the
        // measured center on phase B, then extrapolate one frame with the
        // measured plane velocity on phase A. This removes the hold/jump pulse
        // without adding a third surface query.
        if (completedContactCycle)
        {
            const int32_t measuredRaw = rideTarget.RawValue();
            if (!ioState.committedSurfaceYValid)
            {
                ioState.committedSurfaceYRaw = measuredRaw;
                ioState.surfaceTargetVelocityRaw = 0;
                ioState.committedSurfaceYValid = true;
            }
            else
            {
                const int32_t elapsedFrames = useReducedProbe ? 2 : 1;
                const int32_t measuredVelocityRaw =
                    (measuredRaw - ioState.committedSurfaceYRaw) / elapsedFrames;
                ioState.surfaceTargetVelocityRaw = std::clamp<int32_t>(
                    measuredVelocityRaw,
                    -Tunables::kMaxYStepUpPerFrame.RawValue(),
                    Tunables::kMaxYStepDownPerFrame.RawValue());
                ioState.committedSurfaceYRaw = measuredRaw;
            }
        }
        else if (ioState.committedSurfaceYValid &&
                 ioState.surfaceYInitialized)
        {
            rideTarget = Fxp::BuildRaw(
                ioState.surfaceYTarget.RawValue() +
                ioState.surfaceTargetVelocityRaw);
        }
        // Do NOT clamp climb targets: soft-capping UP made steep ramps tunnel
        // (body never saw the full MapHeight rise and stuck inside asphalt).
        // Descent keeps full target; climb adhesion rate-limits in ApplyVerticalAdhesion
        // except hard anti-penetration snap.
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
            // Live decline chord under axles (Yf > Yr in Y-down).
            if (frontValid && rearValid)
            {
                const int32_t chord =
                    frontY.RawValue() - rearY.RawValue();
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
        if (ioState.gradeValid)
        {
            // tan * 100 ≈ (gradeTanRaw * 100) >> 16
            const int32_t g100 =
                static_cast<int32_t>((static_cast<int64_t>(ioState.gradeTanRaw) * 100) >> 16);
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
