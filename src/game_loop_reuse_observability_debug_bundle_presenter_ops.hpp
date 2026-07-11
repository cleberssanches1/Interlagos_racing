#pragma once

#include "game_loop_reuse_observability_debug_bundle_assembler.hpp"
#include "game_loop_reuse_observability_debug_presenter_ops.hpp"
#include "game_loop_reuse_runtime_debug_bridge_assembler.hpp"
#include "game_loop_track_reuse_runtime_state_contracts.hpp"
#include "game_loop_track_reuse_runtime_state_ops.hpp"

namespace GameLoopObservabilityDomain
{

inline void PresentReuseObservabilityDebugBundle(const ReuseObservabilityDebugBundle& bundle)
{
    if (!bundle.valid)
    {
        return;
    }

    if (bundle.debug.valid)
    {
        PresentReuseObservabilityDebugPacket(bundle.debug);
        return;
    }

    PresentReuseObservabilityDebugPacket(BuildReuseObservabilityDebugPacket(bundle.reuse));
}

inline bool TryPresentReuseObservabilityDebugBundle(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    ReuseObservabilityDebugBundle bundle{};
    if (!TryBuildReuseObservabilityDebugBundle(runtimeOwnerPacket, bundle))
    {
        return false;
    }

    PresentReuseObservabilityDebugBundle(bundle);
    return true;
}

inline bool TryPresentTrackReuseObservabilityDebugBundle(
    const GameLoopRuntime::TrackReuseRuntimeState& runtimeState,
    bool lockstepEnabled = true)
{
    return TryPresentReuseObservabilityDebugBundle(
        GameLoopRuntime::BuildTrackReuseRuntimeOwnerPacket(runtimeState, lockstepEnabled));
}

} // namespace GameLoopObservabilityDomain
