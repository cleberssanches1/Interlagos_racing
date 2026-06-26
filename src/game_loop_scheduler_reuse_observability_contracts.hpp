#pragma once

#include "game_loop_reuse_observability_contracts.hpp"
#include "game_loop_simulation_scheduler_telemetry_view_contracts.hpp"
#include "game_loop_track_render_producer_state_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct SchedulerReuseObservabilityPacket
{
    bool valid = false;
    GameLoopRuntime::SimulationSchedulerTelemetryViewPacket simulationScheduler{};
    GameLoopRuntime::TrackRenderProducerStatePacket trackProducerState{};
    ReuseObservabilityPacket reuse{};
};

} // namespace GameLoopObservabilityDomain
