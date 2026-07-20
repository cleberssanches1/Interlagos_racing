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

        const int16_t steeringAbs = (ioFrameState.steering < 0)
            ? static_cast<int16_t>(-ioFrameState.steering)
            : ioFrameState.steering;
        const bool lowDynamicsInputs =
            (!ioFrameState.braking) &&
            (steeringAbs <= Tunables::kLowDynamicsSteeringThreshold) &&
            (ioFrameState.speedProxy <= Tunables::kLowDynamicsSpeedProxyThreshold);
        const bool mediumDynamicsInputs =
            (!ioFrameState.braking) &&
            (steeringAbs <= Tunables::kMediumDynamicsSteeringThreshold) &&
            (ioFrameState.speedProxy >= Tunables::kMediumDynamicsSpeedProxyMin) &&
            (ioFrameState.speedProxy <= Tunables::kMediumDynamicsSpeedProxyMax);
        // Always sample surface Y every frame. On Saturn low-cost: always use
        // 2 centerline probes (front/rear) — 4 corner probes + heavy surface
        // scans destroyed FPS on declines. Pitch still works from front/rear.
        const bool useReducedProbe =
            Tunables::kEnableSaturnLowCostPhysics ||
            (lowDynamicsInputs &&
             (steeringAbs <= 20) &&
             (ioFrameState.speedProxy <= 40));
        ioState.surfaceProbeCooldown = 0u;
        (void)mediumDynamicsInputs;

        int32_t sampledSegmentId = -1;
        if (sampledSegmentId <= 0)
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
            }
            else
            {
                ioState.verticalVelocity = Fxp::BuildRaw(0);
                ioState.surfaceYFilterInitialized = false;
            }
            return;
        }

        // Soft follow (cheap): full snap for small deltas, 1/2 step for larger
        // segment-seam jumps. Avoids stairs without multi-segment re-scans.
        const Fxp targetY = ioState.surfaceYTarget;
        if (!ioState.surfaceYFilterInitialized)
        {
            ioState.surfaceYFiltered = targetY;
            ioState.surfaceYFilterInitialized = true;
        }
        else
        {
            const int32_t cur = ioState.surfaceYFiltered.RawValue();
            const int32_t tgt = targetY.RawValue();
            int32_t d = tgt - cur;
            const int32_t ad = (d < 0) ? -d : d;
            // ~0.5 world units: continuous grade within a face.
            if (ad <= (1 << 15))
            {
                ioState.surfaceYFiltered = targetY;
            }
            else
            {
                // Approach halfway each frame — smooths package seams.
                ioState.surfaceYFiltered = Fxp::BuildRaw(cur + (d >> 1));
            }
        }
        ioCarWorldPosition.Y = ioState.surfaceYFiltered;
        ioState.verticalVelocity = Fxp::BuildRaw(0);
        ioState.surfaceContactFrames = 8u;

        if (ioState.hasGroundSupport)
        {
            ioState.lastStableX = ioCarWorldPosition.X;
            ioState.lastStableZ = ioCarWorldPosition.Z;
            ioState.lastStablePlanarInitialized = true;
        }
        // Intentionally do NOT teleport XZ back to lastStable when support drops.
    }

    static void Reset(GroundState& ioState)
    {
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
    }

private:
    struct SurfaceProbeSample
    {
        Fxp y = Fxp::BuildRaw(0);
        int32_t segmentId = -1;
        bool valid = false;
    };

    static bool TryProbeSurfaceY(const ITrackCollisionQuery* trackQuery,
                                 const Vector3D& worldPosition,
                                 int32_t seedSegmentId,
                                 SurfaceProbeSample& outSample)
    {
        if (!trackQuery) return false;

        Vector3D samplePosition = worldPosition;
        samplePosition.Y -= Tunables::kSurfaceSampleDownBias;

        outSample.segmentId = -1;
        if constexpr (Tunables::kEnableSurfaceTypeQuery)
        {
            outSample.valid = trackQuery->SampleSurfaceYBySurfaceTypeSetStrict(
                samplePosition,
                Tunables::kDriveableSurfaceTypes.data(),
                Tunables::kDriveableSurfaceTypes.size(),
                outSample.y,
                &outSample.segmentId,
                seedSegmentId);
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

        const int32_t seedSegmentId = (ioState.lastSurfaceSegmentId > 0)
            ? static_cast<int32_t>(ioState.lastSurfaceSegmentId)
            : sampledSegmentId;

        const Fxp longFront = Tunables::kProbeHalfWheelBase;
        const Fxp longRear = Fxp::BuildRaw(-Tunables::kProbeHalfWheelBase.RawValue());
        const Fxp latLeft = Fxp::BuildRaw(-Tunables::kProbeHalfTrack.RawValue());
        const Fxp latRight = Tunables::kProbeHalfTrack;

        SurfaceProbeSample fl{};
        SurfaceProbeSample fr{};
        SurfaceProbeSample rl{};
        SurfaceProbeSample rr{};
        if (useReducedProbe)
        {
            SurfaceProbeSample fc{};
            SurfaceProbeSample rc{};
            const Fxp latCenter = Fxp::BuildRaw(0);
            (void)TryProbeSurfaceY(trackQuery,
                                   BuildProbePoint(worldPosition, sinYaw, cosYaw, longFront, latCenter),
                                   seedSegmentId,
                                   fc);
            (void)TryProbeSurfaceY(trackQuery,
                                   BuildProbePoint(worldPosition, sinYaw, cosYaw, longRear, latCenter),
                                   seedSegmentId,
                                   rc);
            fl = fc;
            fr = fc;
            rl = rc;
            rr = rc;
        }
        else
        {
            (void)TryProbeSurfaceY(trackQuery, BuildProbePoint(worldPosition, sinYaw, cosYaw, longFront, latLeft), seedSegmentId, fl);
            (void)TryProbeSurfaceY(trackQuery, BuildProbePoint(worldPosition, sinYaw, cosYaw, longFront, latRight), seedSegmentId, fr);
            (void)TryProbeSurfaceY(trackQuery, BuildProbePoint(worldPosition, sinYaw, cosYaw, longRear, latLeft), seedSegmentId, rl);
            (void)TryProbeSurfaceY(trackQuery, BuildProbePoint(worldPosition, sinYaw, cosYaw, longRear, latRight), seedSegmentId, rr);
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

        const Fxp fallbackY = ioState.surfaceYInitialized ? ioState.surfaceYTarget : worldPosition.Y;
        const Fxp rearY = AveragePairY(rl, rr, fallbackY);
        const Fxp frontY = AveragePairY(fl, fr, fallbackY);
        const Fxp leftY = AveragePairY(fl, rl, fallbackY);
        const Fxp rightY = AveragePairY(fr, rr, fallbackY);

        ioFrameState.debugGroundYRear = rearValid ? FxpToDebugInt(rearY) : 0;
        ioFrameState.debugGroundYFront = frontValid ? FxpToDebugInt(frontY) : 0;
        ioFrameState.debugGroundYRearRaw = rearValid ? rearY.RawValue() : 0;
        ioFrameState.debugGroundYFrontRaw = frontValid ? frontY.RawValue() : 0;

        ioState.lastSlopeAbsY = 0;
        if (frontValid && rearValid)
        {
            int32_t d = static_cast<int32_t>(ioFrameState.debugGroundYFront) -
                        static_cast<int32_t>(ioFrameState.debugGroundYRear);
            if (d < 0) d = -d;
            if (d > 32767) d = 32767;
            ioState.lastSlopeAbsY = static_cast<int16_t>(d);
        }

        if (!ioState.hasGroundSupport)
        {
            // Keep last surfaceYFiltered for contact hold — do not zero it.
            ioState.surfaceYInitialized = false;
            ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYFiltered);
            return;
        }

        int32_t validCount = 0;
        int64_t sumYRaw = 0;
        if (fl.valid) { sumYRaw += static_cast<int64_t>(fl.y.RawValue()); ++validCount; }
        if (fr.valid) { sumYRaw += static_cast<int64_t>(fr.y.RawValue()); ++validCount; }
        if (rl.valid) { sumYRaw += static_cast<int64_t>(rl.y.RawValue()); ++validCount; }
        if (rr.valid) { sumYRaw += static_cast<int64_t>(rr.y.RawValue()); ++validCount; }
        if (validCount <= 0)
        {
            ioState.surfaceYInitialized = false;
            ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYFiltered);
            return;
        }

        // Body height from front/rear centerline (matches 2-probe Saturn path).
        // When all 4 corners are valid, blend lightly with average for roll.
        const Fxp avgY = Fxp::BuildRaw(static_cast<int32_t>(sumYRaw / validCount));
        const Fxp centerlineY = (frontY + rearY) / 2;
        Fxp blendedY = centerlineY;
        if (frontValid && rearValid && leftValid && rightValid)
        {
            const Fxp lateralY = (leftY + rightY) / 2;
            blendedY = Fxp::BuildRaw(static_cast<int32_t>(
                (static_cast<int64_t>(avgY.RawValue()) +
                 static_cast<int64_t>(centerlineY.RawValue()) +
                 static_cast<int64_t>(lateralY.RawValue())) / 3));
        }
        else if (!frontValid || !rearValid)
        {
            blendedY = avgY;
        }

        ioState.surfaceYTarget = blendedY + GetRideHeightOffset();
        ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYTarget);
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
