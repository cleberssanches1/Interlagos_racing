#pragma once

#include <cstdint>

#include "interfaces.hpp"

namespace Game
{

struct InputFramePacket
{
    uint32_t frameId = 0u;
    GameplayFrameState gameplayFrame{};
};

struct SimulationFramePacket
{
    uint32_t frameId = 0u;
    GameplayFrameState gameplayFrame{};
    bool valid = false;
};

struct CameraFramePacket
{
    uint32_t frameId = 0u;
    Vector3D location{};
    Vector3D lookTarget{};
    bool ready = false;
};

struct TrackRenderPacket
{
    uint32_t frameId = 0u;
    int16_t activeSegmentId = -1;
    bool valid = false;
};

struct CarRenderPacket
{
    uint32_t frameId = 0u;
    Vector3D worldPosition{};
    int32_t yawDeg = 0;
    bool valid = false;
};

struct HudFramePacket
{
    uint32_t frameId = 0u;
    int16_t speedKmh = 0;
    int16_t engineRpm = 0;
    int16_t gear = 0;
    bool valid = false;
};

struct SimulationSchedulerPolicy
{
    uint32_t softSpinLimit = 512u * 1024u;
    uint32_t hardSpinLimit = 8u * 1024u * 1024u;
    uint8_t slaveBackoffFrames = 6u;
    bool lockstep = true;
};

enum class SimulationDispatchMode : uint8_t
{
    Synchronous = 0,
    SlaveLockstep,
    SlaveAsync
};

} // namespace Game
