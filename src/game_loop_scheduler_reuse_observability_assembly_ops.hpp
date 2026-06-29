#pragma once

#include "game_loop_reuse_observability_assembler.hpp"
#include "game_loop_scheduler_reuse_debug_telemetry_assembler.hpp"
#include "game_loop_scheduler_reuse_flow_observability_assembler.hpp"
#include "game_loop_scheduler_reuse_observability_assembler.hpp"
#include "game_loop_simulation_scheduler_lifecycle_observability_assembler.hpp"

namespace GameLoopObservabilityDomain
{

struct SchedulerReuseObservabilityAssemblyInputs
{
    GameLoopRuntime::SimulationDrainViewPacket drain{};
    GameLoopRuntime::SimulationCompletionViewPacket completion{};
    GameLoopRuntime::SimulationSchedulerTelemetryViewPacket simulationTelemetry{};
    GameLoopRuntime::TrackRenderProducerStatePacket trackProducerState{};
    GameLoopRuntime::SimulationReuseDecisionViewPacket simulationReuseDecision{};
    GameLoopRuntime::SimulationReuseTelemetryViewPacket simulationReuseTelemetry{};
    GameLoopRuntime::TrackReuseDecisionViewPacket trackReuseDecision{};
    GameLoopRuntime::TrackReuseTelemetryViewPacket trackReuseTelemetry{};
    GameLoopRuntime::TrackRenderTelemetryViewPacket trackTelemetry{};
    bool includeQueryTelemetry = false;
};

struct SchedulerReuseObservabilityAssemblyBundle
{
    bool valid = false;
    ReuseObservabilityPacket reuse{};
    SimulationSchedulerLifecycleObservabilityPacket lifecycle{};
    SchedulerReuseObservabilityPacket schedulerReuse{};
    SchedulerReuseFlowObservabilityPacket flow{};
    GameLoopTelemetryDomain::SchedulerReuseDebugTelemetryPacket debug{};
};

inline void SeedSchedulerReuseObservabilityAssemblyBundle(
    const SchedulerReuseObservabilityAssemblyInputs& inputs,
    SchedulerReuseObservabilityAssemblyBundle& outBundle)
{
    outBundle.reuse = BuildReuseObservabilityPacket(inputs.simulationReuseDecision,
                                                    inputs.simulationReuseTelemetry,
                                                    inputs.trackReuseDecision,
                                                    inputs.trackReuseTelemetry);
    outBundle.lifecycle =
        BuildSimulationSchedulerLifecycleObservabilityPacket(inputs.drain,
                                                             inputs.completion,
                                                             inputs.simulationTelemetry);
    outBundle.schedulerReuse =
        BuildSchedulerReuseObservabilityPacket(inputs.simulationTelemetry,
                                               inputs.trackProducerState,
                                               outBundle.reuse);
    outBundle.flow =
        BuildSchedulerReuseFlowObservabilityPacket(outBundle.lifecycle,
                                                   outBundle.schedulerReuse);
    outBundle.debug =
        GameLoopTelemetryDomain::BuildSchedulerReuseDebugTelemetryPacket(
            outBundle.flow,
            inputs.trackTelemetry,
            inputs.includeQueryTelemetry);
    outBundle.valid = outBundle.reuse.valid ||
                      outBundle.lifecycle.valid ||
                      outBundle.schedulerReuse.valid ||
                      outBundle.flow.valid ||
                      outBundle.debug.valid;
}

inline SchedulerReuseObservabilityAssemblyBundle BuildSchedulerReuseObservabilityAssemblyBundle(
    const SchedulerReuseObservabilityAssemblyInputs& inputs)
{
    SchedulerReuseObservabilityAssemblyBundle bundle{};
    SeedSchedulerReuseObservabilityAssemblyBundle(inputs, bundle);
    return bundle;
}

} // namespace GameLoopObservabilityDomain
