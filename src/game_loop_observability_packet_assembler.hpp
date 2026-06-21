#pragma once

#include "game_loop_observability_state_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline FrameObservabilityPacket BuildFrameObservabilityPacket(
    uint32_t frameId,
    const GameLoopOverlayDomain::SegmentOverlayPacket& segment,
    const GameLoopOverlayDomain::WindowOverlayPacket& window,
    const GameLoopOverlayDomain::NearestSegmentPacket& nearest,
    const GameLoopOverlayDomain::OverlayDiagnosticsPacket& diagnostics,
    const GameLoopOverlayDomain::OverlayEventPacket& events,
    const GameLoopTelemetryDomain::OverlayFaceShadowPacket& faceShadow,
    const GameLoopTelemetryDomain::OverlayGroundProbePacket& groundProbe,
    const GameLoopTelemetryDomain::OverlayPhysicsQueryPacket& physicsQuery,
    const GameLoopTelemetryDomain::OverlaySegmentEventPacket& segmentEvent,
    const GameLoopTelemetryDomain::Sh2TelemetryPacket& sh2,
    const GameLoopTelemetryDomain::RealtimeFpsPacket& realtimeFps,
    const GameLoopRuntime::TrackRenderFramePacket& trackPacket,
    const GameLoopRuntime::CarVisualFramePacket& carPacket,
    const GameLoopMemoryPresentationDomain::WorkRamUsagePacket& workRamUsage,
    const GameLoopMemoryPresentationDomain::LowWorkOverlayPacket& lowWorkOverlay,
    const GameLoopMemoryPresentationDomain::HighWorkTracePacket& highWorkTrace,
    const GameLoopMemoryPresentationDomain::LowWorkTracePacket& lowWorkTrace)
{
    return BuildFrameObservabilityPacket(
        frameId,
        BuildOverlayPacketFlow(segment, window, nearest, diagnostics, events),
        BuildTelemetryPacketFlow(faceShadow, groundProbe, physicsQuery, segmentEvent, sh2, realtimeFps),
        BuildRenderBudgetPacketFlow(trackPacket, carPacket),
        BuildMemoryPresentationPacketFlow(workRamUsage, lowWorkOverlay, highWorkTrace, lowWorkTrace));
}

} // namespace GameLoopObservabilityDomain
