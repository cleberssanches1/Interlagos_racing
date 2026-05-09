#pragma once

#include "car_physics_shared.hpp"

namespace Game::CarPhysics
{
class GroundFollower
{
public:
    static int32_t UpdateTarget(const ITrackCollisionQuery* trackQuery,
                                const Vector3D& worldPosition,
                                const FrameStepOutput& step,
                                GroundState& ioState,
                                GameplayFrameState& ioFrameState)
    {
        if (!trackQuery)
        {
            return (ioState.lastSurfaceSegmentId > 0)
                ? static_cast<int32_t>(ioState.lastSurfaceSegmentId)
                : -1;
        }

        int32_t sampledSegmentId = -1;
        Vector3D sampledNormal{};
        (void)trackQuery->Sample(worldPosition, sampledNormal, sampledSegmentId);

        const bool segmentChanged =
            (sampledSegmentId > 0) &&
            (sampledSegmentId != static_cast<int32_t>(ioState.lastSurfaceSegmentId));
        const bool movingFast = step.speedAbs >= Tunables::kFastProbeSpeedThreshold;

        if (movingFast || ioState.surfaceProbeCooldown == 0u || segmentChanged || !ioState.surfaceYInitialized)
        {
            ProbeSurfaceTarget(trackQuery,
                               worldPosition,
                               step.sinYaw,
                               step.cosYaw,
                               step.speedAbs,
                               sampledSegmentId,
                               ioState,
                               ioFrameState);
            ioState.surfaceProbeCooldown = movingFast ? 0u : Tunables::kSurfaceProbeIntervalFrames;
        }
        else
        {
            --ioState.surfaceProbeCooldown;
        }

        if (sampledSegmentId <= 0 && ioState.lastSurfaceSegmentId > 0)
        {
            sampledSegmentId = static_cast<int32_t>(ioState.lastSurfaceSegmentId);
        }
        return sampledSegmentId;
    }

    static void ApplyVerticalAdhesion(const GroundState& state, Vector3D& ioCarWorldPosition)
    {
        if (!state.surfaceYInitialized) return;

        // World convention in this project: negative Y is up, larger Y is down.
        // Therefore:
        // - deltaY > 0  => road is below the car (descending / drop ahead)
        // - deltaY < 0  => road is above the car (climbing)
        Fxp deltaY = state.surfaceYTarget - ioCarWorldPosition.Y;
        if (deltaY > Tunables::kSnapDownThreshold)
        {
            // Large downhill gap: snap down to keep tire contact and avoid
            // "flying" over descending faces.
            ioCarWorldPosition.Y = state.surfaceYTarget;
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
    }

    static void Reset(GroundState& ioState)
    {
        ioState.surfaceYTarget = Fxp::BuildRaw(0);
        ioState.lastSurfaceSegmentId = -1;
        ioState.surfaceProbeCooldown = 0u;
        ioState.auxProbeCooldown = 0u;
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
        outSample.valid = trackQuery->SampleSurfaceYByFamilySet(samplePosition,
                                                                Tunables::kDriveableFamilies.data(),
                                                                Tunables::kDriveableFamilies.size(),
                                                                outSample.y,
                                                                &outSample.segmentId,
                                                                seedSegmentId);
        return outSample.valid;
    }

    static void ProbeSurfaceTarget(const ITrackCollisionQuery* trackQuery,
                                   const Vector3D& worldPosition,
                                   const Fxp& sinYaw,
                                   const Fxp& cosYaw,
                                   const Fxp& speedAbs,
                                   int32_t sampledSegmentId,
                                   GroundState& ioState,
                                   GameplayFrameState& ioFrameState)
    {
        ResetGroundDebug(ioFrameState);
        SurfaceProbeSample centerSample{};
        SurfaceProbeSample frontSample{};
        const int32_t seedSegmentId = (sampledSegmentId > 0)
            ? sampledSegmentId
            : static_cast<int32_t>(ioState.lastSurfaceSegmentId);
        const bool centerValid = TryProbeSurfaceY(trackQuery, worldPosition, seedSegmentId, centerSample);

        if (centerValid) ioFrameState.debugGroundMask |= 0x1u;
        ioFrameState.debugGroundYRear = centerValid ? FxpToDebugInt(centerSample.y) : 0;
        ioFrameState.debugGroundYFront = 0;

        bool frontValid = false;
        const bool segmentChanged = (sampledSegmentId > 0) &&
            (sampledSegmentId != static_cast<int32_t>(ioState.lastSurfaceSegmentId));
        const bool shouldProbeFront =
            (speedAbs >= Tunables::kProbeSlopeAssistSpeed) &&
            (ioState.auxProbeCooldown == 0u || segmentChanged || !centerValid);
        if (shouldProbeFront)
        {
            const Fxp negCosYaw = Fxp::BuildRaw(-cosYaw.RawValue());
            const Fxp frontDistance =
                Clamp(Tunables::kProbeFrontBase + (speedAbs * Tunables::kProbeFrontSpeedScale),
                      Tunables::kProbeFrontMin,
                      Tunables::kProbeFrontMax);
            Vector3D frontProbePos = worldPosition;
            frontProbePos.X += sinYaw * frontDistance;
            frontProbePos.Z += negCosYaw * frontDistance;
            frontValid = TryProbeSurfaceY(trackQuery, frontProbePos, seedSegmentId, frontSample);
            if (frontValid)
            {
                ioFrameState.debugGroundMask |= 0x4u;
                ioFrameState.debugGroundYFront = FxpToDebugInt(frontSample.y);
            }
            ioState.auxProbeCooldown = Tunables::kAuxProbeCadenceFrames;
        }
        else if (ioState.auxProbeCooldown > 0u)
        {
            --ioState.auxProbeCooldown;
        }

        if (!centerValid && !frontValid)
        {
            return;
        }

        Fxp targetY = centerValid ? centerSample.y : frontSample.y;
        int32_t targetSegmentId = centerValid ? centerSample.segmentId : frontSample.segmentId;

        if (centerValid && frontValid)
        {
            const uint8_t baseAffinity = SegmentAffinityScore(centerSample.segmentId, seedSegmentId);
            const uint8_t frontAffinity = SegmentAffinityScore(frontSample.segmentId, seedSegmentId);
            const bool frontAllowed = !(baseAffinity > 0u && frontAffinity == 0u);
            if (frontAllowed && frontSample.y > targetY)
            {
                targetY = frontSample.y;
                targetSegmentId = frontSample.segmentId;
            }
        }

        ioState.surfaceYTarget = targetY + Tunables::kRideHeightOffset;
        ioFrameState.debugGroundYTarget = FxpToDebugInt(ioState.surfaceYTarget);
        ioState.surfaceYInitialized = true;
        if (targetSegmentId > 0)
        {
            ioState.lastSurfaceSegmentId = static_cast<int16_t>(targetSegmentId);
        }
    }
};
} // namespace Game::CarPhysics
