#pragma once

#include "game_loop_reuse_runtime_debug_bundle_assembler.hpp"
#include "game_loop_reuse_runtime_observability_ops.hpp"
#include "game_loop_reuse_runtime_owner_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline ReuseObservabilityAssemblyInputs CaptureReuseObservabilityAssemblyInputs(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return CaptureReuseObservabilityAssemblyInputs(
        CaptureReuseObservabilitySourceState(
            GameLoopObservabilityDomain::BuildReuseObservabilitySourceOwnerPacket(
                runtimeOwnerPacket)));
}

inline bool TryBuildReuseObservabilityDebugBundle(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket,
    ReuseObservabilityDebugBundle& outBundle)
{
    return TryBuildReuseObservabilityDebugBundle(
        CaptureReuseObservabilityAssemblyInputs(runtimeOwnerPacket),
        outBundle);
}

} // namespace GameLoopObservabilityDomain
