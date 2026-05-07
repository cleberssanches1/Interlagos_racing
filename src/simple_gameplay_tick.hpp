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
        if (!spawnInitialized_)
        {
            spawnPosition_ = ioFrameState.carWorldPosition;
            spawnYawDeg_ = ioFrameState.carYawDeg;
            spawnInitialized_ = true;
        }

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
                // Keep respawn anchor near current route progression.
                spawnPosition_ = ioFrameState.carWorldPosition;
                spawnYawDeg_ = ioFrameState.carYawDeg;
            }
        }

        // Safety respawn when car drifts too far from track center area.
        const SRL::Math::Types::Fxp maxDrift = SRL::Math::Types::Fxp::BuildRaw(0x4E200000); // 20000
        const Vector3D driftDelta = ioFrameState.carWorldPosition - spawnPosition_;
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

        // Vertical follow is handled by car physics.
    }

private:
    Vector3D spawnPosition_{0.0, 0.0, 0.0};
    int32_t spawnYawDeg_ = 0;
    int32_t lastCheckpointSegmentId_ = -1;
    bool spawnInitialized_ = false;
};
} // namespace Game
