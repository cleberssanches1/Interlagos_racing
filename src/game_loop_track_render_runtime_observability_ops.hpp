#pragma once

#include "game_loop_runtime_state.hpp"
#include "game_loop_track_render_presentation_observability_presenter_ops.hpp"
#include "game_loop_track_render_producer_hint_contracts.hpp"
#include "game_loop_track_render_sh2_presentation_assembler.hpp"
#include "game_loop_track_render_telemetry_view_assembler.hpp"
#include "track_render_transition_ops.hpp"
#include "track_system.hpp"

namespace GameLoopRuntime
{

inline bool TryBuildTrackRenderTelemetryViewPacket(const TrackSystem* trackSystem,
                                                   bool trackSystemReady,
                                                   bool renderTrack,
                                                   TrackRenderTelemetryViewPacket& outTelemetryView)
{
    if (!trackSystem || !trackSystemReady || !renderTrack)
    {
        outTelemetryView = {};
        return false;
    }

    const auto trackTelemetry = TrackRenderDomain::BuildTrackRenderTelemetry(*trackSystem);
    outTelemetryView = BuildTrackRenderTelemetryViewPacket(trackTelemetry);
    return true;
}

inline bool TryBuildTrackRenderProducerHintPacket(const TrackSystem* trackSystem,
                                                  bool trackSystemReady,
                                                  bool renderTrack,
                                                  TrackRenderProducerHintPacket& outProducerHint)
{
    TrackRenderTelemetryViewPacket trackTelemetryView{};
    if (!TryBuildTrackRenderTelemetryViewPacket(trackSystem,
                                                trackSystemReady,
                                                renderTrack,
                                                trackTelemetryView))
    {
        outProducerHint = {};
        return false;
    }

    outProducerHint.producerJobInFlight = trackTelemetryView.producerJobInFlight;
    return true;
}

inline bool TryBuildTrackRenderSh2PresentationPacket(
    const Sh2SplitTelemetrySnapshot& sh2Snapshot,
    const TrackRenderTelemetryViewPacket& trackTelemetryView,
    bool useSafeTelemetryFormat,
    uint32_t slaveDispatchCount,
    uint32_t slaveDispatchSkipsTrackBusy,
    GameLoopObservabilityDomain::TrackRenderSh2PresentationPacket& outPacket)
{
    if (!trackTelemetryView.valid)
    {
        outPacket = {};
        return false;
    }

    outPacket = GameLoopObservabilityDomain::BuildTrackRenderSh2PresentationPacket(
        sh2Snapshot,
        useSafeTelemetryFormat,
        slaveDispatchCount,
        slaveDispatchSkipsTrackBusy,
        true,
        trackTelemetryView.producerJobInFlight,
        trackTelemetryView.producerSafeModeActive);
    return outPacket.valid;
}

} // namespace GameLoopRuntime
