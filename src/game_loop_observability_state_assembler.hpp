#pragma once

#include "game_loop_observability_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedOverlayPacketFlow(const GameLoopOverlayDomain::SegmentOverlayPacket& segment,
                                  const GameLoopOverlayDomain::WindowOverlayPacket& window,
                                  const GameLoopOverlayDomain::NearestSegmentPacket& nearest,
                                  const GameLoopOverlayDomain::OverlayDiagnosticsPacket& diagnostics,
                                  const GameLoopOverlayDomain::OverlayEventPacket& events,
                                  OverlayPacketFlow& outFlow)
{
    outFlow.valid = true;
    outFlow.segment = segment;
    outFlow.window = window;
    outFlow.nearest = nearest;
    outFlow.diagnostics = diagnostics;
    outFlow.events = events;
}

inline void SeedTelemetryPacketFlow(const GameLoopTelemetryDomain::OverlayFaceShadowPacket& faceShadow,
                                    const GameLoopTelemetryDomain::OverlayGroundProbePacket& groundProbe,
                                    const GameLoopTelemetryDomain::OverlayPhysicsQueryPacket& physicsQuery,
                                    const GameLoopTelemetryDomain::OverlaySegmentEventPacket& segmentEvent,
                                    const GameLoopTelemetryDomain::Sh2TelemetryPacket& sh2,
                                    const GameLoopTelemetryDomain::RealtimeFpsPacket& realtimeFps,
                                    TelemetryPacketFlow& outFlow)
{
    outFlow.valid = true;
    outFlow.faceShadow = faceShadow;
    outFlow.groundProbe = groundProbe;
    outFlow.physicsQuery = physicsQuery;
    outFlow.segmentEvent = segmentEvent;
    outFlow.sh2 = sh2;
    outFlow.realtimeFps = realtimeFps;
}

inline void SeedMemoryPresentationPacketFlow(
    const GameLoopMemoryPresentationDomain::WorkRamUsagePacket& workRamUsage,
    const GameLoopMemoryPresentationDomain::LowWorkOverlayPacket& lowWorkOverlay,
    const GameLoopMemoryPresentationDomain::HighWorkTracePacket& highWorkTrace,
    const GameLoopMemoryPresentationDomain::LowWorkTracePacket& lowWorkTrace,
    MemoryPresentationPacketFlow& outFlow)
{
    outFlow.valid = true;
    outFlow.workRamUsage = workRamUsage;
    outFlow.lowWorkOverlay = lowWorkOverlay;
    outFlow.highWorkTrace = highWorkTrace;
    outFlow.lowWorkTrace = lowWorkTrace;
}

inline void SeedFrameObservabilityPacket(uint32_t frameId,
                                         const OverlayPacketFlow& overlay,
                                         const TelemetryPacketFlow& telemetry,
                                         const MemoryPresentationPacketFlow& memory,
                                         FrameObservabilityPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.frameId = frameId;
    outPacket.overlay = overlay;
    outPacket.telemetry = telemetry;
    outPacket.memory = memory;
}

} // namespace GameLoopObservabilityDomain
