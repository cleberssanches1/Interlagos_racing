#pragma once

#include "frame_reuse_observability_source_contracts.hpp"

namespace FrameReuseDomain
{

struct ReuseObservabilitySourceOwnerPacket
{
    bool valid = false;
    ReuseObservabilitySourceSnapshot source{};
};

} // namespace FrameReuseDomain
