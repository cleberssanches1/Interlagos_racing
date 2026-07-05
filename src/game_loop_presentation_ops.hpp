#pragma once

#include <cstdint>

#include "game_loop_debug_ops.hpp"
#include "game_loop_runtime_state.hpp"
#include "game_loop_simulation_scheduler_lifecycle_observability_contracts.hpp"
#include "game_loop_simulation_scheduler_telemetry_view_assembler.hpp"
#include "game_loop_track_render_telemetry_view_assembler.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"
#include "simulation_scheduler_telemetry_assembler.hpp"
#include "track_render_contracts.hpp"

namespace GameLoopRuntime
{

inline FramePresentationSnapshot BuildFramePresentationSnapshot(uint32_t submittedTrackFaces,
                                                                uint32_t submittedCarFaces,
                                                                bool runtimeStatsEnabled,
                                                                const Sh2SplitTelemetrySnapshot& sh2)
{
    FramePresentationSnapshot snapshot{};
    snapshot.submittedTrackFaces = ClampToU16(submittedTrackFaces);
    snapshot.submittedCarFaces = ClampToU16(submittedCarFaces);
    snapshot.sh2 = sh2;
    snapshot.SetRuntimeStatsEnabled(runtimeStatsEnabled);
    return snapshot;
}

inline Sh2SplitTelemetrySnapshot BuildSh2SplitTelemetrySnapshot(
    const SimulationSchedulerTelemetryViewPacket& simTelemetry,
    const TrackRenderTelemetryViewPacket& trackTelemetry,
    bool includeQueryTelemetry)
{
    Sh2SplitTelemetrySnapshot snapshot{};
    snapshot.trackMasterTicks = trackTelemetry.masterFrameTicks;
    snapshot.trackSlaveProducerTicks = trackTelemetry.slaveProducerTicks;
    snapshot.trackSlaveSortTicks = trackTelemetry.slaveSortTicks;
    snapshot.trackSlavePlanTicks = trackTelemetry.slavePlanTicks;
    snapshot.simSlaveTicks = simTelemetry.slaveLastJobTicksThisFrame;
    snapshot.simMasterWaitTicks = simTelemetry.masterWaitTicksThisFrame;
    PopulateSh2BusyMetrics(snapshot);
    if (includeQueryTelemetry)
    {
        PopulateSh2QueryTelemetry(trackTelemetry, snapshot);
    }
    snapshot.SetValid(true);
    return snapshot;
}

inline Sh2SplitTelemetrySnapshot BuildSh2SplitTelemetrySnapshot(
    const SimulationSchedulerDomain::SimulationSchedulerTelemetry& simTelemetry,
    const TrackRenderDomain::TrackRenderTelemetry& trackTelemetry,
    bool includeQueryTelemetry)
{
    return BuildSh2SplitTelemetrySnapshot(
        BuildSimulationSchedulerTelemetryViewPacket(simTelemetry),
        BuildTrackRenderTelemetryViewPacket(trackTelemetry),
        includeQueryTelemetry);
}

inline Sh2SplitTelemetrySnapshot BuildSh2SplitTelemetrySnapshot(
    const SimulationSchedulerDomain::SimulationSchedulerTelemetry& simTelemetry,
    const TrackRenderTelemetryViewPacket& trackTelemetry,
    bool includeQueryTelemetry)
{
    return BuildSh2SplitTelemetrySnapshot(
        BuildSimulationSchedulerTelemetryViewPacket(simTelemetry),
        trackTelemetry,
        includeQueryTelemetry);
}

inline Sh2SplitTelemetrySnapshot BuildSh2SplitTelemetrySnapshot(
    const GameLoopObservabilityDomain::SimulationSchedulerLifecycleObservabilityPacket& lifecycle,
    const TrackRenderTelemetryViewPacket& trackTelemetry,
    bool includeQueryTelemetry)
{
    return BuildSh2SplitTelemetrySnapshot(
        lifecycle.telemetry,
        trackTelemetry,
        includeQueryTelemetry);
}

} // namespace GameLoopRuntime
