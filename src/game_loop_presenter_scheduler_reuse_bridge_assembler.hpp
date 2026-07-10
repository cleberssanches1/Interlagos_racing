#pragma once

#include "game_loop_presenter_observability_input_assembler.hpp"
#include "game_loop_scheduler_reuse_runtime_debug_bridge_assembler.hpp"

namespace GameLoopRuntime
{

inline PresenterObservabilityInputPacket BuildPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildPresenterObservabilityInputPacket(
        overlay,
        observability,
        BuildSchedulerReuseRuntimeDebugTelemetryPacket(
            schedulerTelemetry,
            hasProducerState,
            producerJobInFlight,
            producerSafeModeActive,
            runtimeOwnerPacket));
}

inline PresenterObservabilityInputPacket BuildPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const GameLoopObservabilityDomain::SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildPresenterObservabilityInputPacket(
        overlay,
        observability,
        BuildSchedulerReuseRuntimeDebugTelemetryPacket(
            lifecycle,
            schedulerTelemetry,
            hasProducerState,
            producerJobInFlight,
            producerSafeModeActive,
            runtimeOwnerPacket));
}

} // namespace GameLoopRuntime
