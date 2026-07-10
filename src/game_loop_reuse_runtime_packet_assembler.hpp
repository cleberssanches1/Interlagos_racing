#pragma once

#include "game_loop_reuse_observability_assembler.hpp"
#include "game_loop_reuse_runtime_observability_contracts.hpp"
#include "game_loop_simulation_reuse_decision_view_assembler.hpp"
#include "game_loop_simulation_reuse_telemetry_view_assembler.hpp"
#include "game_loop_track_reuse_decision_view_assembler.hpp"
#include "game_loop_track_reuse_telemetry_view_assembler.hpp"

namespace GameLoopObservabilityDomain
{

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

} // namespace GameLoopObservabilityDomain
