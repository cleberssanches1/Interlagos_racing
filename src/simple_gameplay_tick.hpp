#pragma once

#include "interfaces.hpp"

namespace Game
{
// Minimal gameplay state machine with checkpoint counting and safe respawn.
class SimpleGameplayTick final : public IGameplayTick
{
public:
    static constexpr uint16_t kAsphaltFamilyId = 0u;

    void Tick(GameplayFrameState& ioFrameState, const ITrackCollisionQuery* trackQuery) override
    {
        int32_t segmentId = -1;
        Vector3D surfaceNormal{};
        TrackSurfaceSample surfaceSample{};
        bool hasSurfaceSample = false;
        if (trackQuery)
        {
            hasSurfaceSample = trackQuery->SampleSurface(ioFrameState.carWorldPosition,
                                                         kAsphaltFamilyId,
                                                         surfaceSample);
            if (hasSurfaceSample)
            {
                surfaceNormal = surfaceSample.surfaceNormal;
                segmentId = surfaceSample.segmentId;
            }
            else
            {
                (void)trackQuery->Sample(ioFrameState.carWorldPosition, surfaceNormal, segmentId);
            }
        }
        ioFrameState.activeSegmentId = segmentId;
        ioFrameState.cachedSurfaceSample = surfaceSample;
        ioFrameState.cachedSurfaceFamilyId = kAsphaltFamilyId;
        ioFrameState.hasCachedSurfaceSample = hasSurfaceSample;

        if (ioFrameState.phase == GameplayFrameState::RacePhase::Idle)
        {
            if (ioFrameState.throttle > 0 || ioFrameState.wheelsSpinning)
            {
                ioFrameState.phase = GameplayFrameState::RacePhase::Running;
                ioFrameState.checkpointsPassed = 0;
                lastCheckpointSegmentId_ = segmentId;
            }
        }
        else
        {
            if (segmentId >= 0 && segmentId != lastCheckpointSegmentId_)
            {
                ++ioFrameState.checkpointsPassed;
                lastCheckpointSegmentId_ = segmentId;
            }
        }

        // Safety respawn when car drifts too far from the spawn point.
        const SRL::Math::Types::Fxp maxDrift = SRL::Math::Types::Fxp::BuildRaw(0x012C0000); // 300
        const SRL::Math::Types::Vector3D driftDelta = ioFrameState.carWorldPosition - spawnPosition_;
        if (driftDelta.X.Abs() > maxDrift || driftDelta.Z.Abs() > maxDrift)
        {
            ioFrameState.resetRequested = true;
            ioFrameState.respawnPosition = spawnPosition_;
            ioFrameState.respawnYawDeg = spawnYawDeg_;
            ioFrameState.phase = GameplayFrameState::RacePhase::Idle;
            ioFrameState.checkpointsPassed = 0;
            lastCheckpointSegmentId_ = -1;
            return;
        }
    }

    void SetSpawnPosition(const Vector3D& pos, int32_t yawDeg)
    {
        spawnPosition_ = pos;
        spawnYawDeg_ = yawDeg;
    }

private:
    Vector3D spawnPosition_{0.0, 0.0, 0.0};
    int32_t spawnYawDeg_ = 0;
    int32_t lastCheckpointSegmentId_ = -1;
};
} // namespace Game
