#pragma once

#include "game_loop_reuse_runtime_debug_bundle_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline ReuseObservabilitySourceState CaptureEmptyReuseObservabilitySourceState()
{
    return {};
}

inline ReuseObservabilitySourceState CaptureReuseObservabilitySourceState(
    const FrameReuseDomain::SimulationReuseDecisionPacket* simulationDecision,
    const FrameReuseDomain::TrackReuseDecisionPacket* trackDecision,
    const FrameReuseDomain::FrameReuseTelemetry* telemetry)
{
    ReuseObservabilitySourceState state{};
    state.simulationDecision = simulationDecision;
    state.trackDecision = trackDecision;
    state.telemetry = telemetry;
    return state;
}

inline ReuseObservabilityAssemblyInputs CaptureReuseObservabilityAssemblyInputs(
    const ReuseObservabilitySourceState& sourceState)
{
    ReuseObservabilityAssemblyInputs inputs{};
    inputs.simulationDecision = sourceState.simulationDecision;
    inputs.trackDecision = sourceState.trackDecision;
    inputs.telemetry = sourceState.telemetry;
    return inputs;
}

} // namespace GameLoopObservabilityDomain
