#pragma once

#include "game_loop_memory_overlay_text_contracts.hpp"
#include "game_loop_memory_trace_text_contracts.hpp"
#include "game_loop_observability_contracts.hpp"

namespace GameLoopMemoryPresentationDomain
{

struct MemoryDebugPresentationBundle
{
    bool valid = false;
    GameLoopObservabilityDomain::MemoryPresentationPacketFlow memoryFlow{};
    LowWorkOverlayTextBundle overlayText{};
    HighWorkTraceTextPacket highTraceText{};
    LowWorkTraceTextPacket lowTraceText{};
};

} // namespace GameLoopMemoryPresentationDomain
