#pragma once

#include <cstdint>

#include "track_system.hpp"

namespace TrackRenderDomain
{

enum class Stage : uint8_t
{
    FrameContextAssembly = 0,
    PacketAssembly,
    TelemetryAssembly,
    Schedule
};

struct Ports
{
    TrackSystem* track = nullptr;
};

struct TrackFrameContext
{
    bool renderEnabled = false;
    uint32_t frameId = 0u;
    int32_t observedCarSegmentId = -1;
    SRL::Math::Types::Vector3D trackOffset{};
    SRL::Math::Types::Vector3D lightDirection{};
    SRL::Math::Types::Vector3D cameraLocation{};
    SRL::Math::Types::Vector3D cameraLookTarget{};
    SRL::Math::Types::Vector3D carWorldPosition{};
};

struct TrackRenderPacket
{
    bool valid = false;
    int32_t observedCarSegmentId = -1;
    uint16_t submittedTrackFaces = 0u;
    bool usedSlaveProducer = false;
    bool usedSlaveSort = false;
};

struct TrackRenderTelemetry
{
    uint16_t masterFrameTicks = 0u;
    uint16_t slaveProducerTicks = 0u;
    uint16_t slaveSortTicks = 0u;
    uint16_t slavePlanTicks = 0u;
    uint16_t queryCalls = 0u;
    uint16_t queryGlobal = 0u;
    uint16_t queryScmap = 0u;
    uint16_t queryCacheHits = 0u;
    uint16_t queryCacheMisses = 0u;
    uint16_t wallQueryCalls = 0u;
    uint16_t wallQueryHits = 0u;
    uint32_t producerTimeoutFallbacks = 0u;
    uint32_t producerSafeModeFrames = 0u;
    bool producerSafeModeActive = false;
    bool producerJobInFlight = false;
};

} // namespace TrackRenderDomain
