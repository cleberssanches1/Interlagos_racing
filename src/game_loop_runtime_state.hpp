#pragma once

#include <cstddef>
#include <cstdint>

#include "car_system.hpp"

namespace GameLoopRuntime
{

struct Sh2SplitTelemetrySnapshot
{
    static constexpr uint8_t kValidBit = 1u << 0;
    uint16_t trackMasterTicks = 0;
    uint16_t trackSlaveProducerTicks = 0;
    uint16_t trackSlaveSortTicks = 0;
    uint16_t trackSlavePlanTicks = 0;
    uint16_t simSlaveTicks = 0;
    uint16_t simMasterWaitTicks = 0;
    uint16_t masterBusyTicks = 0;
    uint16_t masterWaitTicks = 0;
    uint16_t slaveWorkTicks = 0;
    uint32_t busyTotal = 0;
    uint8_t masterBusyPct = 0;
    uint8_t slaveWorkPct = 0;
    uint8_t masterWaitPctOfSim = 0;
    uint16_t queryCalls = 0;
    uint16_t queryGlobal = 0;
    uint16_t queryScmap = 0;
    uint8_t flags = 0u;

    bool Valid() const { return (flags & kValidBit) != 0u; }
    void SetValid(bool enabled)
    {
        if (enabled) flags |= kValidBit;
        else flags &= static_cast<uint8_t>(~kValidBit);
    }
};

struct FramePresentationSnapshot
{
    static constexpr uint8_t kRuntimeStatsEnabledBit = 1u << 0;
    uint16_t submittedTrackFaces = 0u;
    uint16_t submittedCarFaces = 0u;
    Sh2SplitTelemetrySnapshot sh2{};
    uint8_t flags = 0u;

    bool RuntimeStatsEnabled() const { return (flags & kRuntimeStatsEnabledBit) != 0u; }
    void SetRuntimeStatsEnabled(bool enabled)
    {
        if (enabled) flags |= kRuntimeStatsEnabledBit;
        else flags &= static_cast<uint8_t>(~kRuntimeStatsEnabledBit);
    }
};

struct RealtimeFpsMetricsSnapshot
{
    uint16_t fpsX10 = 0u;
    uint16_t frameMsX10 = 0u;
    uint16_t vbX100 = 0u;
    uint8_t drop30Pct = 0u;
    uint8_t drop60Pct = 0u;
};

struct CameraPathRouteIndices
{
    size_t prev = 0u;
    size_t nearest = 0u;
    size_t next = 0u;
};

struct FrameInputState
{
    bool bHeld = false;
    bool cHeld = false;
    bool yHeld = false;
    bool xHeld = false;
    bool lHeld = false;
    bool rHeld = false;
    bool leftHeld = false;
    bool rightHeld = false;
};

struct CameraFrameState
{
    SRL::Math::Types::Vector3D location{};
    SRL::Math::Types::Vector3D lookTarget{};
    bool ready = false;
};

struct CarRenderFrameState
{
    Game::CarSystem* car = nullptr;
    SRL::Math::Types::Vector3D renderPosition{
        SRL::Math::Types::Fxp::BuildRaw(0),
        SRL::Math::Types::Fxp::BuildRaw(0),
        SRL::Math::Types::Fxp::BuildRaw(0)};
    Game::CarSystem::RuntimeDebugSnapshot runtimeDebug{};
    int32_t renderYawDeg = 0;
};

} // namespace GameLoopRuntime
