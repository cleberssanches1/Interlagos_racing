#pragma once

#include <cstddef>

#include <srl.hpp>

namespace Game
{
using Vector3D = SRL::Math::Types::Vector3D;

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
    int16_t debugGroundYRear = 0;
    int16_t debugGroundYFront = 0;
    int16_t debugGroundYTarget = 0;
    uint8_t debugGroundMask = 0;
    uint32_t checkpointsPassed = 0;
    RacePhase phase = RacePhase::Idle;
    bool resetRequested = false;
    Vector3D respawnPosition{};
    int32_t respawnYawDeg = 0;
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
    virtual bool SampleSurfaceYByFamilyId(const Vector3D& worldPosition,
                                          uint16_t familyId,
                                          SRL::Math::Types::Fxp& outSurfaceY,
                                          int32_t* outSegmentId = nullptr,
                                          int32_t seedSegmentId = -1) const
    {
        (void)worldPosition;
        (void)familyId;
        (void)seedSegmentId;
        if (outSegmentId) *outSegmentId = -1;
        outSurfaceY = SRL::Math::Types::Fxp::BuildRaw(0);
        return false;
    }
    virtual bool SampleSurfaceYByFamilySet(const Vector3D& worldPosition,
                                           const uint16_t* familyIds,
                                           size_t familyCount,
                                           SRL::Math::Types::Fxp& outSurfaceY,
                                           int32_t* outSegmentId = nullptr,
                                           int32_t seedSegmentId = -1) const
    {
        if (!familyIds || familyCount == 0u)
        {
            if (outSegmentId) *outSegmentId = -1;
            outSurfaceY = SRL::Math::Types::Fxp::BuildRaw(0);
            return false;
        }

        bool found = false;
        SRL::Math::Types::Fxp bestDelta = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        SRL::Math::Types::Fxp bestY = worldPosition.Y;
        int32_t bestSegmentId = -1;

        for (size_t i = 0; i < familyCount; ++i)
        {
            SRL::Math::Types::Fxp candidateY{};
            int32_t candidateSegmentId = -1;
            if (!SampleSurfaceYByFamilyId(worldPosition,
                                          familyIds[i],
                                          candidateY,
                                          &candidateSegmentId,
                                          seedSegmentId))
            {
                continue;
            }

            const SRL::Math::Types::Fxp delta = (candidateY - worldPosition.Y).Abs();
            if (!found || delta < bestDelta)
            {
                found = true;
                bestDelta = delta;
                bestY = candidateY;
                bestSegmentId = candidateSegmentId;
            }
        }

        if (outSegmentId) *outSegmentId = found ? bestSegmentId : -1;
        if (found) outSurfaceY = bestY;
        return found;
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
