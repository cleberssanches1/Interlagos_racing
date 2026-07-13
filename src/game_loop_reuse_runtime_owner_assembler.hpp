#pragma once

#include "game_loop_reuse_runtime_source_assembler.hpp"
#include "game_loop_reuse_source_owner_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline ReuseObservabilitySourceOwnerPacket BuildReuseObservabilitySourceOwnerPacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    ReuseObservabilitySourceOwnerPacket packet{};
    packet.source = BuildReuseObservabilitySourcePacket(runtimeOwnerPacket);
    packet.valid = packet.source.hasSimulationDecision ||
                   packet.source.hasTrackDecision ||
                   packet.source.hasTelemetry;
    return packet;
}

} // namespace GameLoopObservabilityDomain
