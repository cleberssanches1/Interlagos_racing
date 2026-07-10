#pragma once

#include "game_loop_simulation_reuse_decision_view_contracts.hpp"
#include "game_loop_simulation_reuse_telemetry_view_contracts.hpp"
#include "game_loop_track_reuse_observability_contracts.hpp"
#include "game_loop_track_reuse_decision_view_contracts.hpp"
#include "game_loop_track_reuse_telemetry_view_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct ReuseObservabilityPacket
{
    bool valid = false;
    GameLoopRuntime::SimulationReuseDecisionViewPacket simulationDecision{};
    GameLoopRuntime::SimulationReuseTelemetryViewPacket simulationTelemetry{};
    GameLoopRuntime::TrackReuseObservabilityPacket track{};
    GameLoopRuntime::TrackReuseDecisionViewPacket trackDecision{};
    GameLoopRuntime::TrackReuseTelemetryViewPacket trackTelemetry{};
};

} // namespace GameLoopObservabilityDomain
