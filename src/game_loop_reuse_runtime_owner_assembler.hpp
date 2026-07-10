#pragma once

#include "game_loop_reuse_runtime_source_assembler.hpp"
#include "game_loop_reuse_source_owner_assembler.hpp"

namespace GameLoopObservabilityDomain
{

inline ReuseObservabilitySourceOwnerPacket BuildReuseObservabilitySourceOwnerPacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildReuseObservabilitySourceOwnerPacket(
        BuildReuseObservabilitySourcePacket(runtimeOwnerPacket));
}

} // namespace GameLoopObservabilityDomain
