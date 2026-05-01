#pragma once

#include <srl.hpp>
#include <cstdint>

namespace Game
{
using Vector3D = SRL::Math::Types::Vector3D;

struct TrackSurfaceSample
{
    Vector3D worldPosition{};
    Vector3D surfaceNormal{0.0, -1.0, 0.0};
    SRL::Math::Types::Fxp surfaceY{SRL::Math::Types::Fxp::BuildRaw(0)};
    int32_t segmentId = -1;
    bool hasSurfaceY = false;
};

struct GameplayFrameState
{
    enum class RacePhase : uint8_t
    {
        Idle = 0,
        Running = 1
    };

    uint32_t frameId = 0;
    Vector3D carWorldPosition{};
    int32_t carYawDeg = 0;
    int16_t throttle = 0;
    int16_t steering = 0;
    bool braking = false;
    bool wheelsSpinning = false;
    int16_t speedProxy = 0;
    int32_t activeSegmentId = -1;
    Vector3D surfaceNormalWorld{0.0, -1.0, 0.0};
    int16_t carPitchDeg = 0;
    int16_t carRollDeg = 0;
    int16_t yawRateDeg = 0;
    uint32_t checkpointsPassed = 0;
    RacePhase phase = RacePhase::Idle;
    bool resetRequested = false;
    Vector3D respawnPosition{};
    int32_t respawnYawDeg = 0;
    TrackSurfaceSample cachedSurfaceSample{};
    uint16_t cachedSurfaceFamilyId = 0u;
    bool hasCachedSurfaceSample = false;
};

struct ICarCommand
{
    virtual ~ICarCommand() = default;
    virtual void Accelerate() = 0;
    virtual void Brake() = 0;
    virtual void SteerLeft() = 0;
    virtual void SteerRight() = 0;
    virtual Vector3D WorldPosition() const = 0;
};

struct ITrackSegment
{
    virtual ~ITrackSegment() = default;
    virtual Vector3D Center() const = 0;
    virtual bool IsReady() const = 0;
    virtual const char* Name() const = 0;
};

struct ICameraTarget
{
    virtual ~ICameraTarget() = default;
    virtual Vector3D TargetPosition() const = 0;
    virtual bool Valid() const = 0;
    virtual const char* Name() const = 0;
};

struct ITrackLoader
{
    virtual ~ITrackLoader() = default;
    virtual bool Load(const char* path) = 0;
};

// Track collision sampling contract used by future physics.
struct ITrackCollisionQuery
{
    virtual ~ITrackCollisionQuery() = default;
    virtual bool Sample(const Vector3D& worldPosition, Vector3D& outSurfaceNormal, int32_t& outSegmentId) const = 0;
    virtual bool SampleSurface(const Vector3D& worldPosition,
                               uint16_t surfaceFamilyId,
                               TrackSurfaceSample& outSample) const
    {
        (void)surfaceFamilyId;
        outSample.worldPosition = worldPosition;
        outSample.surfaceY = worldPosition.Y;
        outSample.hasSurfaceY = false;
        outSample.segmentId = -1;
        outSample.surfaceNormal = Vector3D(0.0, -1.0, 0.0);

        Vector3D normal{};
        int32_t segmentId = -1;
        const bool ok = Sample(worldPosition, normal, segmentId);
        if (ok)
        {
            outSample.surfaceNormal = normal;
            outSample.segmentId = segmentId;
        }
        return ok;
    }
};

// Car physics contract executed each frame.
struct ICarPhysics
{
    virtual ~ICarPhysics() = default;
    virtual void Step(GameplayFrameState& ioFrameState,
                      const ITrackCollisionQuery* trackQuery,
                      Vector3D& ioCarWorldPosition,
                      int32_t& ioCarYawDeg) = 0;
};

// Gameplay logic contract executed before physics each frame.
struct IGameplayTick
{
    virtual ~IGameplayTick() = default;
    virtual void Tick(GameplayFrameState& ioFrameState, const ITrackCollisionQuery* trackQuery) = 0;
};

// Audio event contract executed after render submission each frame.
struct IAudioEvents
{
    virtual ~IAudioEvents() = default;
    virtual void OnFrame(const GameplayFrameState& frameState) = 0;
};
} // namespace Game
