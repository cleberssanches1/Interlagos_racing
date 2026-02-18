#pragma once

#include "interfaces.hpp"

namespace Game
{
// No-op track collision query used as a safe default while physics is pending.
class NullTrackCollisionQuery final : public ITrackCollisionQuery
{
public:
    bool Sample(const Vector3D& worldPosition, Vector3D& outSurfaceNormal, int32_t& outSegmentId) const override
    {
        (void)worldPosition;
        outSurfaceNormal = Vector3D(0.0, -1.0, 0.0);
        outSegmentId = -1;
        return false;
    }
};

// No-op physics implementation that preserves current behavior.
class NullCarPhysics final : public ICarPhysics
{
public:
    void Step(GameplayFrameState& ioFrameState,
              const ITrackCollisionQuery* trackQuery,
              Vector3D& ioCarWorldPosition,
              int32_t& ioCarYawDeg) override
    {
        (void)ioFrameState;
        (void)trackQuery;
        (void)ioCarWorldPosition;
        (void)ioCarYawDeg;
    }
};

// No-op gameplay tick implementation.
class NullGameplayTick final : public IGameplayTick
{
public:
    void Tick(GameplayFrameState& ioFrameState, const ITrackCollisionQuery* trackQuery) override
    {
        (void)ioFrameState;
        (void)trackQuery;
    }
};

// No-op audio event implementation.
class NullAudioEvents final : public IAudioEvents
{
public:
    void OnFrame(const GameplayFrameState& frameState) override
    {
        (void)frameState;
    }
};
} // namespace Game
