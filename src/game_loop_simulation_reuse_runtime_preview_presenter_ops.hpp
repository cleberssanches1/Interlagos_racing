#pragma once

#include <srl.hpp>

#include "game_loop_simulation_reuse_runtime_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void PresentSimulationReuseRuntimeDecisionViewPacket(
    const SimulationReuseDecisionViewPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    SRL::Debug::Print(
        1,
        24,
        "SRD c:%u d:%u l:%u f:%u s:%u/%u",
        static_cast<unsigned>(packet.shouldConsumeCommitted ? 1u : 0u),
        static_cast<unsigned>(packet.shouldDispatchNextFrame ? 1u : 0u),
        static_cast<unsigned>(packet.requiresLockstepWait ? 1u : 0u),
        static_cast<unsigned>(packet.requiresSynchronousFallback ? 1u : 0u),
        static_cast<unsigned>(packet.consumeSlot),
        static_cast<unsigned>(packet.dispatchSlot));
}

inline void PresentSimulationReuseRuntimePreviewPacket(
    const SimulationReuseRuntimePreviewPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    PresentSimulationReuseRuntimeDecisionViewPacket(packet.decisionView);
}

} // namespace GameLoopRuntime
