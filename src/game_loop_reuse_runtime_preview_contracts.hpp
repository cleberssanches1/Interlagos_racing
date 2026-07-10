#pragma once

#include "frame_reuse_runtime_owner_contracts.hpp"
#include "game_loop_reuse_observability_debug_bundle_contracts.hpp"

namespace GameLoopObservabilityDomain
{

struct ReuseRuntimePreviewPacket
{
    bool valid = false;
    FrameReuseDomain::FrameReuseRuntimeOwnerPacket runtimeOwner{};
    ReuseObservabilityDebugBundle debugBundle{};
};

} // namespace GameLoopObservabilityDomain
