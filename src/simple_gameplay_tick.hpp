#pragma once

#include "interfaces.hpp"

namespace Game
{
// Minimal gameplay state machine with checkpoint counting and safe respawn.
class SimpleGameplayTick final : public IGameplayTick
{
public:
    void Tick(GameplayFrameState& ioFrameState, const ITrackCollisionQuery* trackQuery) override
    {
        int32_t segmentId = -1;
        Vector3D surfaceNormal{};
        if (trackQuery)
        {
            (void)trackQuery->Sample(ioFrameState.carWorldPosition, surfaceNormal, segmentId);
        }
        ioFrameState.activeSegmentId = segmentId;

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

        // Safety respawn when car drifts too far from track center area.
        const SRL::Math::Types::Fxp maxDrift = SRL::Math::Types::Fxp::BuildRaw(0x001E0000); // 30
        if (ioFrameState.carWorldPosition.X.Abs() > maxDrift || ioFrameState.carWorldPosition.Z.Abs() > maxDrift)
        {
            ioFrameState.resetRequested = true;
            ioFrameState.respawnPosition = spawnPosition_;
            ioFrameState.respawnYawDeg = spawnYawDeg_;
            ioFrameState.phase = GameplayFrameState::RacePhase::Idle;
            ioFrameState.checkpointsPassed = 0;
            lastCheckpointSegmentId_ = -1;
            return;
        }

        // Keep Y stable for now until full suspension and collision are available.
        ioFrameState.carWorldPosition.Y = spawnPosition_.Y;
    }

private:
    Vector3D spawnPosition_{0.0, 0.0, 0.0};
    int32_t spawnYawDeg_ = 0;
    int32_t lastCheckpointSegmentId_ = -1;
};
} // namespace Game
