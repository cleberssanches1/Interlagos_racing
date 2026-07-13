#pragma once

#include "game_loop_reuse_observability_debug_bundle_assembler.hpp"
#include "game_loop_reuse_observability_debug_presenter_ops.hpp"
#include "game_loop_reuse_runtime_owner_assembler.hpp"
#include "game_loop_track_reuse_runtime_state_contracts.hpp"
#include "game_loop_track_reuse_runtime_state_ops.hpp"

namespace GameLoopObservabilityDomain
{

inline bool TryPresentTrackReuseObservabilityDebugBundle(
    const GameLoopRuntime::TrackReuseRuntimeState& runtimeState,
    bool lockstepEnabled = true)
{
    const ReuseObservabilityDebugBundle bundle = BuildReuseObservabilityDebugBundle(
        CaptureReuseObservabilityAssemblyInputs(
            CaptureReuseObservabilitySourceState(
                GameLoopObservabilityDomain::BuildReuseObservabilitySourceOwnerPacket(
                    GameLoopRuntime::BuildTrackReuseRuntimeOwnerPacket(runtimeState,
                                                                      lockstepEnabled)))));
    if (!bundle.valid)
    {
        return false;
    }

    if (bundle.debug.valid)
    {
        PresentReuseObservabilityDebugPacket(bundle.debug);
        return true;
    }

    PresentReuseObservabilityDebugPacket(BuildReuseObservabilityDebugPacket(bundle.reuse));
    return true;
}

} // namespace GameLoopObservabilityDomain
