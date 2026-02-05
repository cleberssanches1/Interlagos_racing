#pragma once

#include <srl.hpp>

namespace Game
{
using Vector3D = SRL::Math::Types::Vector3D;

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
} // namespace Game
