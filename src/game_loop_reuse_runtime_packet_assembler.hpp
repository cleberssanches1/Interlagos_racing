#pragma once

#include "game_loop_reuse_observability_assembler.hpp"
#include "game_loop_reuse_runtime_observability_contracts.hpp"
#include "game_loop_simulation_reuse_decision_view_assembler.hpp"
#include "game_loop_simulation_reuse_telemetry_view_assembler.hpp"
#include "game_loop_track_reuse_decision_view_assembler.hpp"
#include "game_loop_track_reuse_telemetry_view_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline ReuseObservabilityPacket BuildReuseObservabilityPacket(
    const ReuseObservabilityAssemblyInputs& inputs)
{
    return BuildReuseObservabilityPacket(
        inputs.simulationDecision
            ? GameLoopRuntime::BuildSimulationReuseDecisionViewPacket(*inputs.simulationDecision)
            : GameLoopRuntime::SimulationReuseDecisionViewPacket{},
        inputs.telemetry
            ? GameLoopRuntime::BuildSimulationReuseTelemetryViewPacket(*inputs.telemetry)
            : GameLoopRuntime::SimulationReuseTelemetryViewPacket{},
        inputs.trackDecision
            ? GameLoopRuntime::BuildTrackReuseDecisionViewPacket(*inputs.trackDecision)
            : GameLoopRuntime::TrackReuseDecisionViewPacket{},
        inputs.telemetry
            ? GameLoopRuntime::BuildTrackReuseTelemetryViewPacket(*inputs.telemetry)
            : GameLoopRuntime::TrackReuseTelemetryViewPacket{});
}

} // namespace GameLoopObservabilityDomain
