#pragma once

#include <cstdint>

#include "game_loop_debug_state.hpp"
#include "game_loop_runtime_state.hpp"

namespace GameLoopTelemetryDomain
{

enum class Stage : uint8_t
{
    OverlayFaceShadow = 0,
    OverlayGroundProbe,
    OverlayPhysicsQuery,
    OverlaySegmentEvent,
    Sh2Telemetry,
    RealtimeFps
};

struct OverlayFaceShadowPacket
{
    bool valid = false;
    uint16_t submittedTrackFaces = 0u;
    uint16_t submittedCarFaces = 0u;
    uint16_t submittedFacesTotal = 0u;
    bool sbaLoaded = false;
    bool shadowModelReady = false;
    uint16_t sbaMeshCount = 0u;
    uint16_t sbaFaceCount = 0u;
};

struct OverlayGroundProbePacket
{
    bool valid = false;
    uint16_t groundMask = 0u;
    int32_t deltaX = 0;
    int32_t deltaZ = 0;
    bool wallHit = false;
    int32_t wallPushX = 0;
    int32_t wallPushZ = 0;
    int16_t wheelDistFl = 0;
    int16_t wheelDistFr = 0;
    int16_t wheelDistRl = 0;
    int16_t wheelDistRr = 0;
    int16_t wheelSurfYFl = 0;
    int16_t wheelSurfYFr = 0;
    int16_t wheelSurfYRl = 0;
    int16_t wheelSurfYRr = 0;
};

struct OverlayPhysicsQueryPacket
{
    bool valid = false;
    uint16_t queryCalls = 0u;
    uint16_t queryGlobalPasses = 0u;
    uint16_t queryCacheHits = 0u;
    uint16_t queryCacheMisses = 0u;
    uint16_t wallQueryHits = 0u;
    uint16_t wallQueryCalls = 0u;
};

struct OverlaySegmentEventPacket
{
    bool valid = false;
    bool carSegmentChanged = false;
    bool windowStartChanged = false;
    int16_t prevCarSegmentId = -1;
    int16_t nextCarSegmentId = -1;
    int16_t prevWindowStartId = -1;
    int16_t nextWindowStartId = -1;
};

struct Sh2TelemetryPacket
{
    bool valid = false;
    GameLoopRuntime::Sh2SplitTelemetrySnapshot snapshot{};
};

struct RealtimeFpsPacket
{
    bool valid = false;
    GameLoopRuntime::RealtimeFpsMetricsSnapshot snapshot{};
};

} // namespace GameLoopTelemetryDomain
