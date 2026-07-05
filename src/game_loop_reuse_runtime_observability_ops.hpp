#pragma once

#include "frame_reuse_contracts.hpp"
#include "game_loop_reuse_observability_assembler.hpp"
#include "game_loop_reuse_observability_debug_bundle_assembler.hpp"
#include "game_loop_simulation_reuse_decision_view_assembler.hpp"
#include "game_loop_simulation_reuse_telemetry_view_assembler.hpp"
#include "game_loop_track_reuse_decision_view_assembler.hpp"
#include "game_loop_track_reuse_telemetry_view_assembler.hpp"

namespace GameLoopObservabilityDomain
{

struct ReuseObservabilitySourceState
{
    const FrameReuseDomain::SimulationReuseDecisionPacket* simulationDecision = nullptr;
    const FrameReuseDomain::TrackReuseDecisionPacket* trackDecision = nullptr;
    const FrameReuseDomain::FrameReuseTelemetry* telemetry = nullptr;
};

struct ReuseObservabilityAssemblyInputs
{
    const FrameReuseDomain::SimulationReuseDecisionPacket* simulationDecision = nullptr;
    const FrameReuseDomain::TrackReuseDecisionPacket* trackDecision = nullptr;
    const FrameReuseDomain::FrameReuseTelemetry* telemetry = nullptr;
};

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

inline GameLoopRuntime::SimulationReuseDecisionViewPacket
BuildSimulationReuseDecisionViewPacketOrDefault(
    const FrameReuseDomain::SimulationReuseDecisionPacket* decision)
{
    return decision
        ? GameLoopRuntime::BuildSimulationReuseDecisionViewPacket(*decision)
        : GameLoopRuntime::SimulationReuseDecisionViewPacket{};
}

inline GameLoopRuntime::TrackReuseDecisionViewPacket
BuildTrackReuseDecisionViewPacketOrDefault(
    const FrameReuseDomain::TrackReuseDecisionPacket* decision)
{
    return decision
        ? GameLoopRuntime::BuildTrackReuseDecisionViewPacket(*decision)
        : GameLoopRuntime::TrackReuseDecisionViewPacket{};
}

inline GameLoopRuntime::SimulationReuseTelemetryViewPacket
BuildSimulationReuseTelemetryViewPacketOrDefault(
    const FrameReuseDomain::FrameReuseTelemetry* telemetry)
{
    return telemetry
        ? GameLoopRuntime::BuildSimulationReuseTelemetryViewPacket(*telemetry)
        : GameLoopRuntime::SimulationReuseTelemetryViewPacket{};
}

inline GameLoopRuntime::TrackReuseTelemetryViewPacket
BuildTrackReuseTelemetryViewPacketOrDefault(
    const FrameReuseDomain::FrameReuseTelemetry* telemetry)
{
    return telemetry
        ? GameLoopRuntime::BuildTrackReuseTelemetryViewPacket(*telemetry)
        : GameLoopRuntime::TrackReuseTelemetryViewPacket{};
}

inline ReuseObservabilityPacket BuildReuseObservabilityPacket(
    const ReuseObservabilityAssemblyInputs& inputs)
{
    return BuildReuseObservabilityPacket(
        BuildSimulationReuseDecisionViewPacketOrDefault(inputs.simulationDecision),
        BuildSimulationReuseTelemetryViewPacketOrDefault(inputs.telemetry),
        BuildTrackReuseDecisionViewPacketOrDefault(inputs.trackDecision),
        BuildTrackReuseTelemetryViewPacketOrDefault(inputs.telemetry));
}

inline ReuseObservabilityDebugBundle BuildReuseObservabilityDebugBundle(
    const ReuseObservabilityAssemblyInputs& inputs)
{
    return BuildReuseObservabilityDebugBundle(
        BuildReuseObservabilityPacket(inputs));
}

inline bool TryBuildReuseObservabilityDebugBundle(
    const ReuseObservabilityAssemblyInputs& inputs,
    ReuseObservabilityDebugBundle& outBundle)
{
    outBundle = BuildReuseObservabilityDebugBundle(inputs);
    return outBundle.valid;
}

} // namespace GameLoopObservabilityDomain
