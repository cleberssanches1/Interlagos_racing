#pragma once

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
        if (trackQuery->SampleSurfaceYByFamilyId(samplePosition,
                                                 Tunables::kAsphaltFamilyId,
                                                 asphaltY,
                                                 &asphaltSegmentId,
                                                 seedSegmentId))
        {
            const Fxp deltaY = (asphaltY - samplePosition.Y).Abs();
            if (deltaY < Fxp::BuildRaw(0x0000C000)) // 0.75
            {
                return Tunables::kGripScaleAsphalt;
            }
        }

        Fxp driveableY{};
        int32_t driveableSegmentId = -1;
        if (trackQuery->SampleSurfaceYByFamilySet(samplePosition,
                                                  Tunables::kDriveableFamilies.data(),
                                                  Tunables::kDriveableFamilies.size(),
                                                  driveableY,
                                                  &driveableSegmentId,
                                                  seedSegmentId))
        {
            return Tunables::kGripScaleOffroad;
        }
        return Tunables::kGripScaleFallback;
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

        int32_t sampledSegmentId = -1;
        Vector3D sampledNormal{};
        (void)trackQuery->Sample(worldPosition, sampledNormal, sampledSegmentId);

        ProbeSurfaceTarget(trackQuery,
                           worldPosition,
                           step.sinYaw,
                           step.cosYaw,
                           sampledSegmentId,
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
        ioCarWorldPosition.X += ioState.correctionX;
        ioCarWorldPosition.Z += ioState.correctionZ;
        ioState.correctionX = Fxp::BuildRaw(0);
        ioState.correctionZ = Fxp::BuildRaw(0);

        if (!ioState.surfaceYInitialized)
        {
            if (!ioState.hasGroundSupport && ioState.lastStablePlanarInitialized)
            {
                ioCarWorldPosition.X = ioState.lastStableX;
                ioCarWorldPosition.Z = ioState.lastStableZ;
            }
            return;
        }

        Fxp deltaY = ioState.surfaceYTarget - ioCarWorldPosition.Y;
        if (deltaY > Tunables::kSnapDownThreshold)
        {
            ioCarWorldPosition.Y = ioState.surfaceYTarget;
            deltaY = Fxp::BuildRaw(0);
        }
        else if (deltaY < Fxp::BuildRaw(-Tunables::kSnapUpThreshold.RawValue()))
        {
            ioCarWorldPosition.Y = ioState.surfaceYTarget;
            deltaY = Fxp::BuildRaw(0);
        }

        if (deltaY > Tunables::kMaxYStepDownPerFrame)
        {
            deltaY = Tunables::kMaxYStepDownPerFrame;
        }
        else
        {
            const Fxp minStepUp = Fxp::BuildRaw(-Tunables::kMaxYStepUpPerFrame.RawValue());
            if (deltaY < minStepUp)
            {
                deltaY = minStepUp;
            }
        }

        ioCarWorldPosition.Y += deltaY;

        if (ioState.hasGroundSupport)
        {
            ioState.lastStableX = ioCarWorldPosition.X;
            ioState.lastStableZ = ioCarWorldPosition.Z;
            ioState.lastStablePlanarInitialized = true;
        }
        else if (ioState.lastStablePlanarInitialized)
        {
            ioCarWorldPosition.X = ioState.lastStableX;
            ioCarWorldPosition.Z = ioState.lastStableZ;
        }
    }

    static void Reset(GroundState& ioState)
    {
        ioState.surfaceYTarget = Fxp::BuildRaw(0);
        ioState.correctionX = Fxp::BuildRaw(0);
        ioState.correctionZ = Fxp::BuildRaw(0);
        ioState.lastStableX = Fxp::BuildRaw(0);
        ioState.lastStableZ = Fxp::BuildRaw(0);
        ioState.lastSurfaceSegmentId = -1;
        ioState.surfaceProbeCooldown = 0u;
        ioState.auxProbeCooldown = 0u;
        ioState.hasGroundSupport = false;
        ioState.edgeLeftLost = false;
        ioState.edgeRightLost = false;
        ioState.lastStablePlanarInitialized = false;
        ioState.surfaceYInitialized = false;
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
        outSample.valid = trackQuery->SampleSurfaceYByFamilySetStrict(samplePosition,
                                                                       Tunables::kDriveableFamilies.data(),
                                                                       Tunables::kDriveableFamilies.size(),
                                                                       outSample.y,
                                                                       &outSample.segmentId,
                                                                       seedSegmentId);
        if (outSample.valid) return true;

        // Soft fallback: accept non-strict result only when still near seed.
        SRL::Math::Types::Fxp fallbackY{};
        int32_t fallbackSegmentId = -1;
        if (trackQuery->SampleSurfaceYByFamilySet(samplePosition,
                                                  Tunables::kDriveableFamilies.data(),
                                                  Tunables::kDriveableFamilies.size(),
                                                  fallbackY,
                                                  &fallbackSegmentId,
                                                  seedSegmentId))
        {
            const uint8_t affinity = SegmentAffinityScore(fallbackSegmentId, seedSegmentId);
            if (affinity > 0u)
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
        (void)TryProbeSurfaceY(trackQuery, BuildProbePoint(worldPosition, sinYaw, cosYaw, longFront, latLeft), seedSegmentId, fl);
        (void)TryProbeSurfaceY(trackQuery, BuildProbePoint(worldPosition, sinYaw, cosYaw, longFront, latRight), seedSegmentId, fr);
        (void)TryProbeSurfaceY(trackQuery, BuildProbePoint(worldPosition, sinYaw, cosYaw, longRear, latLeft), seedSegmentId, rl);
        (void)TryProbeSurfaceY(trackQuery, BuildProbePoint(worldPosition, sinYaw, cosYaw, longRear, latRight), seedSegmentId, rr);

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

        if (!ioState.hasGroundSupport)
        {
            ioState.surfaceYInitialized = false;
            ioFrameState.debugGroundYTarget = 0;
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
            ioFrameState.debugGroundYTarget = 0;
            return;
        }

        const Fxp avgY = Fxp::BuildRaw(static_cast<int32_t>(sumYRaw / validCount));
        const Fxp centerlineY = (frontY + rearY) / 2;
        Fxp lateralY = centerlineY;
        if (leftValid && rightValid)
        {
            lateralY = (leftY + rightY) / 2;
        }
        else if (leftValid)
        {
            lateralY = leftY;
        }
        else if (rightValid)
        {
            lateralY = rightY;
        }
        const int64_t blendedYRaw =
            (static_cast<int64_t>(avgY.RawValue()) +
             static_cast<int64_t>(centerlineY.RawValue()) +
             static_cast<int64_t>(lateralY.RawValue())) / 3;
        const Fxp blendedY = Fxp::BuildRaw(static_cast<int32_t>(blendedYRaw));
        ioState.surfaceYTarget = blendedY + GetRideHeightOffset();
        ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYTarget);
        ioState.surfaceYInitialized = true;

        const int32_t targetSegmentId =
            ResolveSegmentId(fl, fr, rl, rr,
                             (sampledSegmentId > 0) ? sampledSegmentId : static_cast<int32_t>(ioState.lastSurfaceSegmentId));
        if (targetSegmentId > 0)
        {
            ioState.lastSurfaceSegmentId = static_cast<int16_t>(targetSegmentId);
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

        const bool shouldQueryWalls =
            ioState.hasGroundSupport && Tunables::kEnableWallPlanarPush;
        if (shouldQueryWalls)
        {
            Vector3D wallPush{};
            const Vector3D forwardDirection =
                Vector3D(sinYaw, Fxp::BuildRaw(0), Fxp::BuildRaw(-cosYaw.RawValue()));
            if (trackQuery->ResolvePlanarWallPush(worldPosition,
                                                  forwardDirection,
                                                  Tunables::kWallCollisionRadius,
                                                  wallPush,
                                                  nullptr,
                                                  targetSegmentId))
            {
                // Slide on walls: keep only lateral repulsion component.
                const Fxp forwardX = sinYaw;
                const Fxp forwardZ = Fxp::BuildRaw(-cosYaw.RawValue());
                const Fxp pushAlongForward = (wallPush.X * forwardX) + (wallPush.Z * forwardZ);
                wallPush.X -= (forwardX * pushAlongForward);
                wallPush.Z -= (forwardZ * pushAlongForward);
                ioState.correctionX += wallPush.X;
                ioState.correctionZ += wallPush.Z;
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
