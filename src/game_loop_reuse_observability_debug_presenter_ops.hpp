#pragma once

#include <srl.hpp>

#include "game_loop_reuse_observability_debug_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void PresentReuseObservabilityDebugPacket(
    const ReuseObservabilityDebugPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    SRL::Debug::Print(1, 26, "RU s:%u%u%u t:%u%u%u",
                      static_cast<unsigned>(packet.simulationShouldConsumeCommitted ? 1u : 0u),
                      static_cast<unsigned>(packet.simulationShouldDispatchNextFrame ? 1u : 0u),
                      static_cast<unsigned>(packet.simulationRequiresSynchronousFallback ? 1u : 0u),
                      static_cast<unsigned>(packet.trackShouldConsumeCommitted ? 1u : 0u),
                      static_cast<unsigned>(packet.trackShouldKickProducer ? 1u : 0u),
                      static_cast<unsigned>(packet.trackRequiresSynchronousFallback ? 1u : 0u));
}

} // namespace GameLoopObservabilityDomain
