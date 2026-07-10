#pragma once

#include "game_loop_presenter_scheduler_reuse_bridge_assembler.hpp"
#include "game_loop_presenter_scheduler_reuse_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterSchedulerReusePreviewPacket(
    const PresenterInputSummaryPacket& summary,
    const PresenterObservabilityInputPacket& observabilityInput,
    const SchedulerReuseDebugTelemetryPacket& schedulerReuseDebug,
    PresenterSchedulerReusePreviewPacket& outPacket)
{
    outPacket.valid = summary.valid || observabilityInput.valid || schedulerReuseDebug.valid;
    outPacket.summary = summary;
    outPacket.observabilityInput = observabilityInput;
    outPacket.schedulerReuseDebug = schedulerReuseDebug;
}

inline PresenterSchedulerReusePreviewPacket BuildPresenterSchedulerReusePreviewPacket(
    const PresenterInputSummaryPacket& summary,
    const PresenterObservabilityInputPacket& observabilityInput,
    const SchedulerReuseDebugTelemetryPacket& schedulerReuseDebug)
{
    PresenterSchedulerReusePreviewPacket packet{};
    SeedPresenterSchedulerReusePreviewPacket(
        summary,
        observabilityInput,
        schedulerReuseDebug,
        packet);
    return packet;
}

inline PresenterSchedulerReusePreviewPacket BuildPresenterSchedulerReusePreviewPacket(
    const PresenterInputSummaryPacket& summary,
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    const auto schedulerReuseDebug = BuildSchedulerReuseRuntimeDebugTelemetryPacket(
        schedulerTelemetry,
        hasProducerState,
        producerJobInFlight,
        producerSafeModeActive,
        runtimeOwnerPacket);
    return BuildPresenterSchedulerReusePreviewPacket(
        summary,
        BuildPresenterObservabilityInputPacket(
            overlay,
            observability,
            schedulerReuseDebug),
        schedulerReuseDebug);
}

inline PresenterSchedulerReusePreviewPacket BuildPresenterSchedulerReusePreviewPacket(
    const PresenterInputSummaryPacket& summary,
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const GameLoopObservabilityDomain::SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    const auto schedulerReuseDebug = BuildSchedulerReuseRuntimeDebugTelemetryPacket(
        lifecycle,
        schedulerTelemetry,
        hasProducerState,
        producerJobInFlight,
        producerSafeModeActive,
        runtimeOwnerPacket);
    return BuildPresenterSchedulerReusePreviewPacket(
        summary,
        BuildPresenterObservabilityInputPacket(
            overlay,
            observability,
            schedulerReuseDebug),
        schedulerReuseDebug);
}

} // namespace GameLoopRuntime
