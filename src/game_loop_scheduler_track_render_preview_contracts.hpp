#pragma once

#include "game_loop_simulation_scheduler_lifecycle_observability_contracts.hpp"
#include "game_loop_track_render_sh2_presentation_contracts.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct SchedulerTrackRenderPreviewPacket
{
    bool valid = false;
    SimulationSchedulerLifecycleObservabilityPacket lifecycle{};
    GameLoopRuntime::TrackRenderTelemetryViewPacket trackTelemetryView{};
    TrackRenderSh2PresentationPacket sh2Presentation{};
};

} // namespace GameLoopObservabilityDomain
