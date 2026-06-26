#pragma once

#include "game_loop_car_visual_packet.hpp"
#include "game_loop_observability_contracts.hpp"
#include "game_loop_render_budget_observability_view_assembler.hpp"
#include "game_loop_track_render_packet.hpp"

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

inline OverlayPacketFlow BuildOverlayPacketFlow(const GameLoopOverlayDomain::SegmentOverlayPacket& segment,
                                                const GameLoopOverlayDomain::WindowOverlayPacket& window,
                                                const GameLoopOverlayDomain::NearestSegmentPacket& nearest,
                                                const GameLoopOverlayDomain::OverlayDiagnosticsPacket& diagnostics,
                                                const GameLoopOverlayDomain::OverlayEventPacket& events)
{
    OverlayPacketFlow flow{};
    SeedOverlayPacketFlow(segment, window, nearest, diagnostics, events, flow);
    return flow;
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

inline TelemetryPacketFlow BuildTelemetryPacketFlow(
    const GameLoopTelemetryDomain::OverlayFaceShadowPacket& faceShadow,
    const GameLoopTelemetryDomain::OverlayGroundProbePacket& groundProbe,
    const GameLoopTelemetryDomain::OverlayPhysicsQueryPacket& physicsQuery,
    const GameLoopTelemetryDomain::OverlaySegmentEventPacket& segmentEvent,
    const GameLoopTelemetryDomain::Sh2TelemetryPacket& sh2,
    const GameLoopTelemetryDomain::RealtimeFpsPacket& realtimeFps)
{
    TelemetryPacketFlow flow{};
    SeedTelemetryPacketFlow(faceShadow, groundProbe, physicsQuery, segmentEvent, sh2, realtimeFps, flow);
    return flow;
}

inline void SeedRenderBudgetPolicyPacket(
    const MemoryBudgetDomain::CategoryBudgetPolicy& budgetPolicy,
    bool valid,
    RenderBudgetPolicyPacket& outPacket)
{
    outPacket.valid = valid;
    outPacket.budgetPolicy = budgetPolicy;
    outPacket.shouldReducePressure = budgetPolicy.shouldReducePressure;
    outPacket.shouldAvoidOptionalAllocations = budgetPolicy.shouldAvoidOptionalAllocations;
}

inline RenderBudgetPolicyPacket BuildRenderBudgetPolicyPacket(
    const MemoryBudgetDomain::CategoryBudgetPolicy& budgetPolicy,
    bool valid)
{
    RenderBudgetPolicyPacket packet{};
    SeedRenderBudgetPolicyPacket(budgetPolicy, valid, packet);
    return packet;
}

inline void SeedRenderBudgetPacketFlow(const GameLoopRuntime::TrackRenderFramePacket& trackPacket,
                                       const GameLoopRuntime::CarVisualFramePacket& carPacket,
                                       RenderBudgetPacketFlow& outFlow)
{
    outFlow.valid = true;
    SeedRenderBudgetPolicyPacket(trackPacket.budgetPolicy, trackPacket.Valid(), outFlow.track);
    SeedRenderBudgetPolicyPacket(carPacket.budgetPolicy, carPacket.Valid(), outFlow.car);
}

inline RenderBudgetPacketFlow BuildRenderBudgetPacketFlow(
    const GameLoopRuntime::TrackRenderFramePacket& trackPacket,
    const GameLoopRuntime::CarVisualFramePacket& carPacket)
{
    RenderBudgetPacketFlow flow{};
    SeedRenderBudgetPacketFlow(trackPacket, carPacket, flow);
    return flow;
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

inline MemoryPresentationPacketFlow BuildMemoryPresentationPacketFlow(
    const GameLoopMemoryPresentationDomain::WorkRamUsagePacket& workRamUsage,
    const GameLoopMemoryPresentationDomain::LowWorkOverlayPacket& lowWorkOverlay,
    const GameLoopMemoryPresentationDomain::HighWorkTracePacket& highWorkTrace,
    const GameLoopMemoryPresentationDomain::LowWorkTracePacket& lowWorkTrace)
{
    MemoryPresentationPacketFlow flow{};
    SeedMemoryPresentationPacketFlow(workRamUsage, lowWorkOverlay, highWorkTrace, lowWorkTrace, flow);
    return flow;
}

inline void SeedFrameObservabilityPacket(uint32_t frameId,
                                         const OverlayPacketFlow& overlay,
                                         const TelemetryPacketFlow& telemetry,
                                         const RenderBudgetPacketFlow& renderBudget,
                                         const MemoryPresentationPacketFlow& memory,
                                         FrameObservabilityPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.frameId = frameId;
    outPacket.overlay = overlay;
    outPacket.telemetry = telemetry;
    outPacket.renderBudget = renderBudget;
    outPacket.memory = memory;
}

inline FrameObservabilityPacket BuildFrameObservabilityPacket(uint32_t frameId,
                                                              const OverlayPacketFlow& overlay,
                                                              const TelemetryPacketFlow& telemetry,
                                                              const RenderBudgetPacketFlow& renderBudget,
                                                              const MemoryPresentationPacketFlow& memory)
{
    FrameObservabilityPacket packet{};
    SeedFrameObservabilityPacket(frameId, overlay, telemetry, renderBudget, memory, packet);
    return packet;
}

inline FrameObservabilityPacket BuildFrameObservabilityPacket(
    uint32_t frameId,
    const OverlayPacketFlow& overlay,
    const TelemetryPacketFlow& telemetry,
    const GameLoopRuntime::TrackRenderFramePacket& trackPacket,
    const GameLoopRuntime::CarVisualFramePacket& carPacket,
    const MemoryPresentationPacketFlow& memory)
{
    return BuildFrameObservabilityPacket(
        frameId,
        overlay,
        telemetry,
        BuildRenderBudgetPacketFlow(trackPacket, carPacket),
        memory);
}

} // namespace GameLoopObservabilityDomain
