#pragma once

#include "game_loop_memory_debug_packet_assembler.hpp"
#include "game_loop_observability_debug_contracts.hpp"
#include "game_loop_observability_packet_assembler.hpp"

namespace GameLoopObservabilityDomain
{

struct ObservabilityFrameInputs
{
    uint32_t frameId = 0u;
    GameLoopOverlayDomain::SegmentOverlayPacket segment{};
    GameLoopOverlayDomain::WindowOverlayPacket window{};
    GameLoopOverlayDomain::NearestSegmentPacket nearest{};
    GameLoopOverlayDomain::OverlayDiagnosticsPacket diagnostics{};
    GameLoopOverlayDomain::OverlayEventPacket events{};
    GameLoopTelemetryDomain::OverlayFaceShadowPacket faceShadow{};
    GameLoopTelemetryDomain::OverlayGroundProbePacket groundProbe{};
    GameLoopTelemetryDomain::OverlayPhysicsQueryPacket physicsQuery{};
    GameLoopTelemetryDomain::OverlaySegmentEventPacket segmentEvent{};
    GameLoopTelemetryDomain::Sh2TelemetryPacket sh2{};
    GameLoopTelemetryDomain::RealtimeFpsPacket realtimeFps{};
    GameLoopRuntime::TrackRenderFramePacket trackPacket{};
    GameLoopRuntime::CarVisualFramePacket carPacket{};
    GameLoopMemoryPresentationDomain::WorkRamUsagePacket workRamUsage{};
    GameLoopMemoryPresentationDomain::LowWorkOverlayPacket lowWorkOverlay{};
    GameLoopMemoryPresentationDomain::HighWorkTracePacket highWorkTrace{};
    GameLoopMemoryPresentationDomain::LowWorkTracePacket lowWorkTrace{};
};

inline void SeedObservabilityDebugBundle(
    const FrameObservabilityPacket& frame,
    const GameLoopMemoryPresentationDomain::MemoryDebugPresentationBundle& memoryDebug,
    ObservabilityDebugBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.frame = frame;
    outBundle.memoryDebug = memoryDebug;
}

inline ObservabilityDebugBundle BuildObservabilityDebugBundle(
    const FrameObservabilityPacket& frame,
    const GameLoopMemoryPresentationDomain::MemoryDebugPresentationBundle& memoryDebug)
{
    ObservabilityDebugBundle bundle{};
    SeedObservabilityDebugBundle(frame, memoryDebug, bundle);
    return bundle;
}

inline ObservabilityDebugBundle BuildObservabilityDebugBundle(
    const ObservabilityFrameInputs& frameInputs,
    const GameLoopMemoryPresentationDomain::MemoryDebugPresentationBundle& memoryDebug)
{
    return BuildObservabilityDebugBundle(
        BuildFrameObservabilityPacket(frameInputs.frameId,
                                      frameInputs.segment,
                                      frameInputs.window,
                                      frameInputs.nearest,
                                      frameInputs.diagnostics,
                                      frameInputs.events,
                                      frameInputs.faceShadow,
                                      frameInputs.groundProbe,
                                      frameInputs.physicsQuery,
                                      frameInputs.segmentEvent,
                                      frameInputs.sh2,
                                      frameInputs.realtimeFps,
                                      frameInputs.trackPacket,
                                      frameInputs.carPacket,
                                      frameInputs.workRamUsage,
                                      frameInputs.lowWorkOverlay,
                                      frameInputs.highWorkTrace,
                                      frameInputs.lowWorkTrace),
        memoryDebug);
}

inline ObservabilityDebugBundle BuildObservabilityDebugBundle(
    const ObservabilityFrameInputs& frameInputs,
    const MemoryBudgetDomain::MemorySnapshotPacket& snapshot,
    const GameLoopMemoryPresentationDomain::LowWorkOverlayAssemblyInputs& overlayPacketInputs,
    const GameLoopMemoryPresentationDomain::MemoryDebugOverlayInputs& overlayTextInputs,
    const GameLoopMemoryPresentationDomain::MemoryDebugTraceInputs& traceInputs)
{
    return BuildObservabilityDebugBundle(
        frameInputs,
        GameLoopMemoryPresentationDomain::BuildMemoryDebugPresentationBundle(
            snapshot,
            overlayPacketInputs,
            overlayTextInputs,
            traceInputs));
}

} // namespace GameLoopObservabilityDomain
