#pragma once

#include "game_loop_scheduler_reuse_debug_telemetry_assembler.hpp"
#include "game_loop_scheduler_reuse_observability_assembly_ops.hpp"

namespace GameLoopRuntime
{

inline SchedulerReuseDebugTelemetryPacket BuildSchedulerReuseRuntimeDebugTelemetryPacket(
    const SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildSchedulerReuseDebugTelemetryPacket(
        GameLoopObservabilityDomain::BuildSchedulerReuseObservabilityPacket(
            schedulerTelemetry,
            hasProducerState,
            producerJobInFlight,
            producerSafeModeActive,
            runtimeOwnerPacket));
}

inline SchedulerReuseDebugTelemetryPacket BuildSchedulerReuseRuntimeDebugTelemetryPacket(
    const GameLoopObservabilityDomain::SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const SimulationSchedulerTelemetryViewPacket& schedulerTelemetry,
    bool hasProducerState,
    bool producerJobInFlight,
    bool producerSafeModeActive,
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildSchedulerReuseDebugTelemetryPacket(
        GameLoopObservabilityDomain::BuildSchedulerReuseFlowObservabilityPacket(
            lifecycle,
            schedulerTelemetry,
            hasProducerState,
            producerJobInFlight,
            producerSafeModeActive,
            runtimeOwnerPacket));
}

} // namespace GameLoopRuntime
