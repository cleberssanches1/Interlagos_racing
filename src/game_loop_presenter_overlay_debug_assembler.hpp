#pragma once

#include "game_loop_overlay_debug_contracts.hpp"
#include "game_loop_observability_debug_contracts.hpp"
#include "game_loop_presenter_overlay_debug_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterOverlayDebugPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlayBundle,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observabilityBundle,
    PresenterOverlayDebugPacket& outPacket)
{
    outPacket.valid = overlayBundle.valid || observabilityBundle.valid;
    outPacket.hasOverlayFlow = overlayBundle.overlayFlow.valid;
    outPacket.hasTelemetryFlow = overlayBundle.telemetryFlow.valid;
    outPacket.hasOverlayText = overlayBundle.text.valid;
    outPacket.hasFrameObservability = observabilityBundle.frame.valid;
    outPacket.hasMemoryDebug = observabilityBundle.memoryDebug.valid;
    outPacket.hasWorkRamUsage = observabilityBundle.memoryDebug.memoryFlow.workRamUsage.valid;
    outPacket.hasLowWorkOverlay = observabilityBundle.memoryDebug.memoryFlow.lowWorkOverlay.valid;
    outPacket.hasHighWorkTrace = observabilityBundle.memoryDebug.memoryFlow.highWorkTrace.valid;
    outPacket.hasLowWorkTrace = observabilityBundle.memoryDebug.memoryFlow.lowWorkTrace.valid;
    outPacket.carSegmentChanged = overlayBundle.overlayFlow.events.carSegmentChanged;
    outPacket.windowStartChanged = overlayBundle.overlayFlow.events.windowStartChanged;
    outPacket.wallHit = overlayBundle.telemetryFlow.groundProbe.wallHit;
    outPacket.frameId = observabilityBundle.frame.frameId;
    outPacket.activeSegmentId = overlayBundle.overlayFlow.nearest.activeSegmentId;
    outPacket.nearestSegmentId = overlayBundle.overlayFlow.nearest.nearestSegmentId;
    outPacket.windowStartId = overlayBundle.overlayFlow.window.windowStartId;
    outPacket.windowCount = overlayBundle.overlayFlow.window.windowCount;
    outPacket.groundMask = overlayBundle.telemetryFlow.groundProbe.groundMask;
    outPacket.submittedFacesTotal = overlayBundle.telemetryFlow.faceShadow.submittedFacesTotal;
    outPacket.queryCalls = overlayBundle.telemetryFlow.physicsQuery.queryCalls;
    outPacket.wallQueryCalls = overlayBundle.telemetryFlow.physicsQuery.wallQueryCalls;
}

inline PresenterOverlayDebugPacket BuildPresenterOverlayDebugPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlayBundle,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observabilityBundle)
{
    PresenterOverlayDebugPacket packet{};
    SeedPresenterOverlayDebugPacket(overlayBundle, observabilityBundle, packet);
    return packet;
}

} // namespace GameLoopRuntime
