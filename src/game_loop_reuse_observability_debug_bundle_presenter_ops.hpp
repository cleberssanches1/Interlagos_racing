#pragma once

#include "game_loop_reuse_observability_debug_bundle_assembler.hpp"
#include "game_loop_reuse_observability_debug_presenter_ops.hpp"

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

} // namespace GameLoopObservabilityDomain
