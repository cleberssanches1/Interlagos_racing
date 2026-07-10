#pragma once

#include "game_loop_reuse_runtime_debug_bridge_assembler.hpp"
#include "game_loop_scheduler_reuse_flow_observability_assembler.hpp"
#include "game_loop_scheduler_reuse_observability_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline SchedulerReuseObservabilityAssemblyInputs CaptureSchedulerReuseObservabilityAssemblyInputs(
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    SchedulerReuseObservabilityAssemblyInputs inputs{};
    inputs.schedulerTelemetry = schedulerTelemetry;
    inputs.hasProducerState = hasProducerState;
    inputs.producerJobInFlight = producerJobInFlight;
    inputs.producerSafeModeActive = producerSafeModeActive;
    inputs.reuse = BuildReuseObservabilityPacket(
        CaptureReuseObservabilityAssemblyInputs(runtimeOwnerPacket));
    return inputs;
}

inline SchedulerReuseObservabilityPacket BuildSchedulerReuseObservabilityPacket(
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildSchedulerReuseObservabilityPacket(
        CaptureSchedulerReuseObservabilityAssemblyInputs(schedulerTelemetry,
                                                         hasProducerState,
                                                         producerJobInFlight,
                                                         producerSafeModeActive,
                                                         runtimeOwnerPacket));
}

inline SchedulerReuseFlowObservabilityPacket BuildSchedulerReuseFlowObservabilityPacket(
    const SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const GameLoopRuntime::SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildSchedulerReuseFlowObservabilityPacket(
        lifecycle,
        BuildSchedulerReuseObservabilityPacket(schedulerTelemetry,
                                               hasProducerState,
                                               producerJobInFlight,
                                               producerSafeModeActive,
                                               runtimeOwnerPacket));
}

} // namespace GameLoopObservabilityDomain
