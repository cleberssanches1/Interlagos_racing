#pragma once

#include "game_loop_observability_contracts.hpp"
#include "game_loop_overlay_debug_text_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedOverlaySpatialTextPacket(const GameLoopRuntime::OverlayDiagnosticsSnapshot& overlay,
                                         int32_t carYawDeg,
                                         OverlaySpatialTextPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.carYawDeg = carYawDeg;
    outPacket.forwardX = overlay.segment.fwdX;
    outPacket.forwardZ = overlay.segment.fwdZ;
    outPacket.camDirX = overlay.segment.camDirX;
    outPacket.camDirZ = overlay.segment.camDirZ;
    outPacket.segY = overlay.segment.segY;
    outPacket.deltaY = overlay.segment.deltaY;
    outPacket.groundRearY = overlay.carDebug.groundRearY;
    outPacket.groundFrontY = overlay.carDebug.groundFrontY;
    outPacket.groundTargetY = overlay.carDebug.groundTargetY;
    outPacket.groundSurfaceType = overlay.carDebug.groundSurfaceType;
    outPacket.groundFamilyId = overlay.carDebug.groundFamilyId;
    outPacket.groundFaceIndex = overlay.carDebug.groundFaceIndex;
}

inline OverlaySpatialTextPacket BuildOverlaySpatialTextPacket(
    const GameLoopRuntime::OverlayDiagnosticsSnapshot& overlay,
    int32_t carYawDeg)
{
    OverlaySpatialTextPacket packet{};
    SeedOverlaySpatialTextPacket(overlay, carYawDeg, packet);
    return packet;
}

inline void SeedOverlayShadowTextPacket(int32_t worldX,
                                        int32_t worldY,
                                        int32_t worldZ,
                                        int32_t yawDeg,
                                        OverlayShadowTextPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.worldX = worldX;
    outPacket.worldY = worldY;
    outPacket.worldZ = worldZ;
    outPacket.yawDeg = yawDeg;
}

inline OverlayShadowTextPacket BuildOverlayShadowTextPacket(int32_t worldX,
                                                            int32_t worldY,
                                                            int32_t worldZ,
                                                            int32_t yawDeg)
{
    OverlayShadowTextPacket packet{};
    SeedOverlayShadowTextPacket(worldX, worldY, worldZ, yawDeg, packet);
    return packet;
}

inline void SeedOverlayFaceShadowTextPacket(const GameLoopTelemetryDomain::OverlayFaceShadowPacket& packet,
                                            OverlayFaceShadowTextPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.submittedTrackFaces = packet.submittedTrackFaces;
    outPacket.submittedCarFaces = packet.submittedCarFaces;
    outPacket.submittedFacesTotal = packet.submittedFacesTotal;
    outPacket.sbaLoaded = packet.sbaLoaded;
    outPacket.shadowModelReady = packet.shadowModelReady;
    outPacket.sbaMeshCount = packet.sbaMeshCount;
    outPacket.sbaFaceCount = packet.sbaFaceCount;
}

inline OverlayFaceShadowTextPacket BuildOverlayFaceShadowTextPacket(
    const GameLoopTelemetryDomain::OverlayFaceShadowPacket& packet)
{
    OverlayFaceShadowTextPacket outPacket{};
    SeedOverlayFaceShadowTextPacket(packet, outPacket);
    return outPacket;
}

inline void SeedOverlayGroundProbeTextPacket(const GameLoopTelemetryDomain::OverlayGroundProbePacket& packet,
                                             OverlayGroundProbeTextPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.groundMask = packet.groundMask;
    outPacket.deltaX = packet.deltaX;
    outPacket.deltaZ = packet.deltaZ;
    outPacket.wallHit = packet.wallHit;
    outPacket.wallPushX = packet.wallPushX;
    outPacket.wallPushZ = packet.wallPushZ;
    outPacket.wheelDistFl = packet.wheelDistFl;
    outPacket.wheelDistFr = packet.wheelDistFr;
    outPacket.wheelDistRl = packet.wheelDistRl;
    outPacket.wheelDistRr = packet.wheelDistRr;
    outPacket.wheelSurfYFl = packet.wheelSurfYFl;
    outPacket.wheelSurfYFr = packet.wheelSurfYFr;
    outPacket.wheelSurfYRl = packet.wheelSurfYRl;
    outPacket.wheelSurfYRr = packet.wheelSurfYRr;
}

inline OverlayGroundProbeTextPacket BuildOverlayGroundProbeTextPacket(
    const GameLoopTelemetryDomain::OverlayGroundProbePacket& packet)
{
    OverlayGroundProbeTextPacket outPacket{};
    SeedOverlayGroundProbeTextPacket(packet, outPacket);
    return outPacket;
}

inline void SeedOverlayPhysicsQueryTextPacket(const GameLoopTelemetryDomain::OverlayPhysicsQueryPacket& packet,
                                              OverlayPhysicsQueryTextPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.queryCalls = packet.queryCalls;
    outPacket.queryGlobalPasses = packet.queryGlobalPasses;
    outPacket.queryCacheHits = packet.queryCacheHits;
    outPacket.queryCacheMisses = packet.queryCacheMisses;
    outPacket.wallQueryHits = packet.wallQueryHits;
    outPacket.wallQueryCalls = packet.wallQueryCalls;
}

inline OverlayPhysicsQueryTextPacket BuildOverlayPhysicsQueryTextPacket(
    const GameLoopTelemetryDomain::OverlayPhysicsQueryPacket& packet)
{
    OverlayPhysicsQueryTextPacket outPacket{};
    SeedOverlayPhysicsQueryTextPacket(packet, outPacket);
    return outPacket;
}

inline void SeedOverlaySegmentEventTextPacket(const GameLoopTelemetryDomain::OverlaySegmentEventPacket& packet,
                                              OverlaySegmentEventTextPacket& outPacket)
{
    outPacket.valid = packet.valid;
    outPacket.carSegmentChanged = packet.carSegmentChanged;
    outPacket.windowStartChanged = packet.windowStartChanged;
    outPacket.prevCarSegmentId = packet.prevCarSegmentId;
    outPacket.nextCarSegmentId = packet.nextCarSegmentId;
    outPacket.prevWindowStartId = packet.prevWindowStartId;
    outPacket.nextWindowStartId = packet.nextWindowStartId;
}

inline OverlaySegmentEventTextPacket BuildOverlaySegmentEventTextPacket(
    const GameLoopTelemetryDomain::OverlaySegmentEventPacket& packet)
{
    OverlaySegmentEventTextPacket outPacket{};
    SeedOverlaySegmentEventTextPacket(packet, outPacket);
    return outPacket;
}

inline void SeedOverlayDebugTextBundle(const OverlaySpatialTextPacket& spatial,
                                       const OverlayShadowTextPacket& shadow,
                                       const OverlayFaceShadowTextPacket& faceShadow,
                                       const OverlayGroundProbeTextPacket& groundProbe,
                                       const OverlayPhysicsQueryTextPacket& physicsQuery,
                                       const OverlaySegmentEventTextPacket& segmentEvent,
                                       OverlayDebugTextBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.spatial = spatial;
    outBundle.shadow = shadow;
    outBundle.faceShadow = faceShadow;
    outBundle.groundProbe = groundProbe;
    outBundle.physicsQuery = physicsQuery;
    outBundle.segmentEvent = segmentEvent;
}

inline OverlayDebugTextBundle BuildOverlayDebugTextBundle(
    const OverlaySpatialTextPacket& spatial,
    const OverlayShadowTextPacket& shadow,
    const OverlayFaceShadowTextPacket& faceShadow,
    const OverlayGroundProbeTextPacket& groundProbe,
    const OverlayPhysicsQueryTextPacket& physicsQuery,
    const OverlaySegmentEventTextPacket& segmentEvent)
{
    OverlayDebugTextBundle bundle{};
    SeedOverlayDebugTextBundle(
        spatial, shadow, faceShadow, groundProbe, physicsQuery, segmentEvent, bundle);
    return bundle;
}

inline OverlayDebugTextBundle BuildOverlayDebugTextBundle(
    const GameLoopRuntime::OverlayDiagnosticsSnapshot& overlay,
    int32_t carYawDeg,
    int32_t shadowWorldX,
    int32_t shadowWorldY,
    int32_t shadowWorldZ,
    int32_t shadowYawDeg,
    const GameLoopTelemetryDomain::OverlayFaceShadowPacket& faceShadow,
    const GameLoopTelemetryDomain::OverlayGroundProbePacket& groundProbe,
    const GameLoopTelemetryDomain::OverlayPhysicsQueryPacket& physicsQuery,
    const GameLoopTelemetryDomain::OverlaySegmentEventPacket& segmentEvent)
{
    return BuildOverlayDebugTextBundle(
        BuildOverlaySpatialTextPacket(overlay, carYawDeg),
        BuildOverlayShadowTextPacket(shadowWorldX, shadowWorldY, shadowWorldZ, shadowYawDeg),
        BuildOverlayFaceShadowTextPacket(faceShadow),
        BuildOverlayGroundProbeTextPacket(groundProbe),
        BuildOverlayPhysicsQueryTextPacket(physicsQuery),
        BuildOverlaySegmentEventTextPacket(segmentEvent));
}

} // namespace GameLoopObservabilityDomain
