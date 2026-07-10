#pragma once

#include "frame_reuse_observability_source_owner_assembler.hpp"
#include "frame_reuse_runtime_observability_source_assembler.hpp"

namespace FrameReuseDomain
{

inline ReuseObservabilitySourceOwnerPacket
BuildReuseObservabilitySourceOwnerPacket(const FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return BuildReuseObservabilitySourceOwnerPacket(
        BuildReuseObservabilitySourceSnapshot(runtimeOwnerPacket));
}

} // namespace FrameReuseDomain
