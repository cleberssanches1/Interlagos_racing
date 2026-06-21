#pragma once

#include "game_loop_memory_debug_contracts.hpp"
#include "game_loop_memory_overlay_text_assembler.hpp"
#include "game_loop_memory_presentation_packet_assembler.hpp"
#include "game_loop_memory_trace_text_assembler.hpp"

namespace GameLoopMemoryPresentationDomain
{

struct MemoryDebugOverlayInputs
{
    LowWorkOverlayHeaderPacket header{};
    HighWorkOverlayPacket highWork{};
    LowWorkOverlayBreakdownPacket breakdown{};
    LowWorkOverlayTicksPacket ticks{};
    LowWorkTagGroupPacket tagGroups{};
    LowWorkAllocatorPacket allocator{};
};

struct MemoryDebugTraceInputs
{
    const GameLoopRuntime::HwrStageTrace* highWorkTrace = nullptr;
    const GameLoopRuntime::LwrStageTrace* lowWorkTrace = nullptr;
    LowWorkTraceDeltaInputs lowWorkTraceDeltas{};
};

inline void SeedMemoryDebugPresentationBundle(
    const GameLoopObservabilityDomain::MemoryPresentationPacketFlow& memoryFlow,
    const LowWorkOverlayTextBundle& overlayText,
    const HighWorkTraceTextPacket& highTraceText,
    const LowWorkTraceTextPacket& lowTraceText,
    MemoryDebugPresentationBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.memoryFlow = memoryFlow;
    outBundle.overlayText = overlayText;
    outBundle.highTraceText = highTraceText;
    outBundle.lowTraceText = lowTraceText;
}

inline MemoryDebugPresentationBundle BuildMemoryDebugPresentationBundle(
    const GameLoopObservabilityDomain::MemoryPresentationPacketFlow& memoryFlow,
    const LowWorkOverlayTextBundle& overlayText,
    const HighWorkTraceTextPacket& highTraceText,
    const LowWorkTraceTextPacket& lowTraceText)
{
    MemoryDebugPresentationBundle bundle{};
    SeedMemoryDebugPresentationBundle(memoryFlow, overlayText, highTraceText, lowTraceText, bundle);
    return bundle;
}

inline MemoryDebugPresentationBundle BuildMemoryDebugPresentationBundle(
    const MemoryBudgetDomain::MemorySnapshotPacket& snapshot,
    const LowWorkOverlayAssemblyInputs& overlayPacketInputs,
    const MemoryDebugOverlayInputs& overlayTextInputs,
    const MemoryDebugTraceInputs& traceInputs)
{
    const auto memoryFlow = BuildObservabilityMemoryPresentationPacketFlow(
        snapshot,
        overlayPacketInputs,
        traceInputs.highWorkTrace ? *traceInputs.highWorkTrace : GameLoopRuntime::HwrStageTrace{},
        traceInputs.lowWorkTrace ? *traceInputs.lowWorkTrace : GameLoopRuntime::LwrStageTrace{});

    const auto overlayText = BuildLowWorkOverlayTextBundle(overlayTextInputs.header,
                                                           overlayTextInputs.highWork,
                                                           overlayTextInputs.breakdown,
                                                           overlayTextInputs.ticks,
                                                           overlayTextInputs.tagGroups,
                                                           overlayTextInputs.allocator);

    const auto highTraceText =
        traceInputs.highWorkTrace
            ? BuildHighWorkTraceTextPacket(BuildHighWorkTracePacket(*traceInputs.highWorkTrace))
            : HighWorkTraceTextPacket{};

    const auto lowTraceText =
        traceInputs.lowWorkTrace
            ? BuildLowWorkTraceTextPacket(BuildLowWorkTracePacket(*traceInputs.lowWorkTrace),
                                          traceInputs.lowWorkTraceDeltas)
            : LowWorkTraceTextPacket{};

    return BuildMemoryDebugPresentationBundle(memoryFlow, overlayText, highTraceText, lowTraceText);
}

} // namespace GameLoopMemoryPresentationDomain
