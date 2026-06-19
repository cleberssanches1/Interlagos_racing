#pragma once

#include "game_loop_overlay_contracts.hpp"
#include "game_loop_telemetry_contracts.hpp"

namespace GameLoopTelemetryDomain
{

inline void SeedOverlayFaceShadowPacket(const GameLoopRuntime::OverlayDiagnosticsSnapshot& overlay,
                                        bool sbaLoaded,
                                        bool shadowModelReady,
                                        uint16_t sbaMeshCount,
                                        uint16_t sbaFaceCount,
                                        OverlayFaceShadowPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.submittedTrackFaces = overlay.submittedTrackFaces;
    outPacket.submittedCarFaces = overlay.submittedCarFaces;
    outPacket.submittedFacesTotal = overlay.submittedFacesTotal;
    outPacket.sbaLoaded = sbaLoaded;
    outPacket.shadowModelReady = shadowModelReady;
    outPacket.sbaMeshCount = sbaMeshCount;
    outPacket.sbaFaceCount = sbaFaceCount;
}

inline void SeedOverlayGroundProbePacket(const GameLoopRuntime::OverlayDiagnosticsSnapshot& overlay,
                                         OverlayGroundProbePacket& outPacket)
{
    outPacket.valid = true;
    outPacket.groundMask = overlay.carDebug.groundMask;
    outPacket.deltaX = overlay.segment.deltaX;
    outPacket.deltaZ = overlay.segment.deltaZ;
    outPacket.wallHit = overlay.carDebug.WallHit();
    outPacket.wallPushX = overlay.carDebug.wallPushX;
    outPacket.wallPushZ = overlay.carDebug.wallPushZ;
}

inline void SeedOverlayPhysicsQueryPacket(const GameLoopRuntime::OverlayDiagnosticsSnapshot& overlay,
                                          OverlayPhysicsQueryPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.queryCalls = overlay.queryCalls;
    outPacket.queryGlobalPasses = overlay.queryGlobalPasses;
    outPacket.queryCacheHits = overlay.queryCacheHits;
    outPacket.queryCacheMisses = overlay.queryCacheMisses;
    outPacket.wallQueryHits = overlay.wallQueryHits;
    outPacket.wallQueryCalls = overlay.wallQueryCalls;
}

inline void SeedOverlaySegmentEventPacket(const GameLoopOverlayDomain::OverlayEventPacket& overlayEvent,
                                          OverlaySegmentEventPacket& outPacket)
{
    outPacket.valid = overlayEvent.valid;
    outPacket.carSegmentChanged = overlayEvent.carSegmentChanged;
    outPacket.windowStartChanged = overlayEvent.windowStartChanged;
    outPacket.prevCarSegmentId = overlayEvent.prevCarSegmentId;
    outPacket.nextCarSegmentId = overlayEvent.nextCarSegmentId;
    outPacket.prevWindowStartId = overlayEvent.prevWindowStartId;
    outPacket.nextWindowStartId = overlayEvent.nextWindowStartId;
}

inline void SeedSh2TelemetryPacket(const GameLoopRuntime::Sh2SplitTelemetrySnapshot& snapshot,
                                   Sh2TelemetryPacket& outPacket)
{
    outPacket.valid = snapshot.Valid();
    outPacket.snapshot = snapshot;
}

inline void SeedRealtimeFpsPacket(const GameLoopRuntime::RealtimeFpsMetricsSnapshot& snapshot,
                                  RealtimeFpsPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.snapshot = snapshot;
}

} // namespace GameLoopTelemetryDomain
