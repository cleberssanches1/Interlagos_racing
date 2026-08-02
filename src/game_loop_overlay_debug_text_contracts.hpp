#pragma once

#include <cstdint>

namespace GameLoopObservabilityDomain
{

struct OverlaySpatialTextPacket
{
    bool valid = false;
    int32_t carYawDeg = 0;
    int32_t forwardX = 0;
    int32_t forwardZ = 0;
    int32_t camDirX = 0;
    int32_t camDirZ = 0;
    int32_t segY = 0;
    int32_t deltaY = 0;
    int32_t groundRearY = 0;
    int32_t groundFrontY = 0;
    int32_t groundTargetY = 0;
    uint16_t groundSurfaceType = 0u;
    uint16_t groundFamilyId = 0u;
    int32_t groundFaceIndex = 0;
};

struct OverlayShadowTextPacket
{
    bool valid = false;
    int32_t worldX = 0;
    int32_t worldY = 0;
    int32_t worldZ = 0;
    int32_t yawDeg = 0;
};

struct OverlayFaceShadowTextPacket
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

struct OverlayGroundProbeTextPacket
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

struct OverlayPhysicsQueryTextPacket
{
    bool valid = false;
    uint16_t queryCalls = 0u;
    uint16_t queryGlobalPasses = 0u;
    uint16_t queryCacheHits = 0u;
    uint16_t queryCacheMisses = 0u;
    uint16_t wallQueryHits = 0u;
    uint16_t wallQueryCalls = 0u;
};

struct OverlaySegmentEventTextPacket
{
    bool valid = false;
    bool carSegmentChanged = false;
    bool windowStartChanged = false;
    int16_t prevCarSegmentId = -1;
    int16_t nextCarSegmentId = -1;
    int16_t prevWindowStartId = -1;
    int16_t nextWindowStartId = -1;
};

struct OverlayDebugTextBundle
{
    bool valid = false;
    OverlaySpatialTextPacket spatial{};
    OverlayShadowTextPacket shadow{};
    OverlayFaceShadowTextPacket faceShadow{};
    OverlayGroundProbeTextPacket groundProbe{};
    OverlayPhysicsQueryTextPacket physicsQuery{};
    OverlaySegmentEventTextPacket segmentEvent{};
};

} // namespace GameLoopObservabilityDomain
