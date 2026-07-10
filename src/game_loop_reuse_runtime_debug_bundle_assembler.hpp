#pragma once

#include "game_loop_reuse_observability_debug_bundle_assembler.hpp"
#include "game_loop_reuse_runtime_packet_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline ReuseObservabilityDebugBundle BuildReuseObservabilityDebugBundle(
    const ReuseObservabilityAssemblyInputs& inputs)
{
    return BuildReuseObservabilityDebugBundle(
        BuildReuseObservabilityPacket(inputs));
}

inline bool TryBuildReuseObservabilityDebugBundle(
    const ReuseObservabilityAssemblyInputs& inputs,
    ReuseObservabilityDebugBundle& outBundle)
{
    outBundle = BuildReuseObservabilityDebugBundle(inputs);
    return outBundle.valid;
}

} // namespace GameLoopObservabilityDomain
