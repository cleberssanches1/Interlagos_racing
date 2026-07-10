#pragma once

#include "frame_reuse_observability_capture_ops.hpp"
#include "track_frame_reuse_ops.hpp"

#include "game_loop_track_reuse_runtime_state_contracts.hpp"

namespace GameLoopRuntime
{

inline void ResetTrackReuseRuntimeState(TrackReuseRuntimeState& ioState)
{
    ioState = TrackReuseRuntimeState{};
}

inline void CaptureTrackReuseRuntimeRequest(uint32_t frameId,
                                            int16_t activeSegmentId,
                                            bool renderEnabled,
                                            bool producerJobInFlight,
                                            TrackReuseRuntimeState& ioState)
{
    ioState.requestFrameId = frameId;
    ioState.activeSegmentId = activeSegmentId;
    ioState.renderEnabled = renderEnabled;
    ioState.producerJobInFlight = producerJobInFlight;
}

inline void CommitTrackReuseRuntimeFrame(uint32_t frameId,
                                         int16_t activeSegmentId,
                                         bool valid,
                                         TrackReuseRuntimeState& ioState)
{
    FrameReuseDomain::CommitTrackFramePacket(frameId,
                                             activeSegmentId,
                                             valid,
                                             ioState.history);
}

inline FrameReuseDomain::TrackReuseDecisionPacket
BuildTrackReuseRuntimeDecisionPacket(const TrackReuseRuntimeState& state,
                                     bool lockstepEnabled = true)
{
    FrameReuseDomain::TrackReuseDecisionPacket trackDecision{};
    FrameReuseDomain::SeedTrackReuseDecisionPacket(state.requestFrameId,
                                                   state.activeSegmentId,
                                                   state.renderEnabled,
                                                   lockstepEnabled,
                                                   state.producerJobInFlight,
                                                   state.history,
                                                   trackDecision);
    return trackDecision;
}

inline FrameReuseDomain::FrameReuseRuntimeOwnerPacket
BuildTrackReuseRuntimeOwnerPacket(const TrackReuseRuntimeState& state,
                                  bool lockstepEnabled = true)
{
    const FrameReuseDomain::TrackReuseDecisionPacket trackDecision =
        BuildTrackReuseRuntimeDecisionPacket(state, lockstepEnabled);
    const FrameReuseDomain::TrackReuseDecisionPacket* trackDecisionPtr =
        trackDecision.valid ? &trackDecision : nullptr;
    return FrameReuseDomain::CaptureFrameReuseRuntimeOwnerPacket(nullptr,
                                                                 &state.history,
                                                                 nullptr,
                                                                 trackDecisionPtr,
                                                                 nullptr);
}

} // namespace GameLoopRuntime
