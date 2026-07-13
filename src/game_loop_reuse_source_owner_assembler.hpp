#pragma once

#include "game_loop_reuse_source_owner_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline ReuseObservabilitySourceOwnerPacket BuildReuseObservabilitySourceOwnerPacket(
    const ReuseObservabilitySourcePacket& sourcePacket)
{
    ReuseObservabilitySourceOwnerPacket packet{};
    packet.valid = sourcePacket.hasSimulationDecision ||
                   sourcePacket.hasTrackDecision ||
                   sourcePacket.hasTelemetry;
    packet.source = sourcePacket;
    return packet;
}

} // namespace GameLoopObservabilityDomain
