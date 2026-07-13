#pragma once

#include "game_loop_memory_debug_contracts.hpp"
#include "game_loop_memory_overlay_text_assembler.hpp"
#include "game_loop_memory_presentation_state_assembler.hpp"
#include "game_loop_observability_state_assembler.hpp"
#include "game_loop_memory_trace_packet_assembler.hpp"
#include "game_loop_memory_trace_text_assembler.hpp"

namespace GameLoopMemoryPresentationDomain
{

struct LowWorkOverlayAssemblyInputs
{
    const GameLoopRuntime::LowWorkOverlayState* overlayState = nullptr;
    int32_t freeDelta = 0;
    uint32_t lowWorkFree = 0u;
    uint32_t highWorkFree = 0u;
    uint8_t slides = 0u;
    int16_t slideId = -1;
    uint16_t trackStreamTicks = 0u;
    uint16_t trackMaintenanceTicks = 0u;
    uint16_t trackDrawTicks = 0u;
    uint16_t trackFrameTicks = 0u;
    uint16_t trackWindowTicks = 0u;
    uint16_t trackPrefetchTicks = 0u;
    uint16_t trackLodTicks = 0u;
    uint16_t trackWorkingSetTicks = 0u;
    uint8_t prefetchBuildAttempts = 0u;
    uint8_t prefetchBuildBudget = 0u;
    uint8_t prefetchBuildDrops = 0u;
    uint32_t knownTaggedBytes = 0u;
    uint32_t invalidTaggedBytes = 0u;
    uint32_t invalidTaggedBlocks = 0u;
};

inline LowWorkOverlayPacket BuildLowWorkOverlayPacket(const LowWorkOverlayAssemblyInputs& inputs)
{
    if (inputs.overlayState == nullptr)
    {
        return {};
    }

    LowWorkOverlayPacket packet = BuildLowWorkOverlayPacket(*inputs.overlayState,
                                                            inputs.freeDelta,
                                                            inputs.lowWorkFree,
                                                            inputs.highWorkFree,
                                                            inputs.slides,
                                                            inputs.slideId);
    AttachLowWorkTrackTicks(inputs.trackStreamTicks,
                            inputs.trackMaintenanceTicks,
                            inputs.trackDrawTicks,
                            inputs.trackFrameTicks,
                            inputs.trackWindowTicks,
                            inputs.trackPrefetchTicks,
                            inputs.trackLodTicks,
                            inputs.trackWorkingSetTicks,
                            inputs.prefetchBuildAttempts,
                            inputs.prefetchBuildBudget,
                            inputs.prefetchBuildDrops,
                            packet);
    AttachLowWorkAllocatorDiagnostics(inputs.knownTaggedBytes,
                                      inputs.invalidTaggedBytes,
                                      inputs.invalidTaggedBlocks,
                                      packet);
    return packet;
}

inline GameLoopObservabilityDomain::MemoryPresentationPacketFlow
BuildObservabilityMemoryPresentationPacketFlow(
    const MemoryBudgetDomain::MemorySnapshotPacket& snapshot,
    const LowWorkOverlayAssemblyInputs& lowWorkOverlay,
    const GameLoopRuntime::HwrStageTrace& highWorkTrace,
    const GameLoopRuntime::LwrStageTrace& lowWorkTrace)
{
    return GameLoopObservabilityDomain::BuildMemoryPresentationPacketFlow(
        BuildWorkRamUsagePacket(snapshot),
        BuildLowWorkOverlayPacket(lowWorkOverlay),
        BuildHighWorkTracePacket(highWorkTrace),
        BuildLowWorkTracePacket(lowWorkTrace));
}

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

inline MemoryDebugPresentationBundle BuildFrameEndMemoryDebugPresentationBundle(
    const GameLoopRuntime::HwrStageTrace& highWorkTrace,
    const GameLoopRuntime::LwrStageTrace& lowWorkTrace,
    const TrackSystem* trackSystem)
{
    const auto memorySnapshot = MemoryBudgetDomain::CaptureMemorySnapshotPacket();
    const LowWorkOverlayAssemblyInputs overlayPacketInputs{};
    const MemoryDebugOverlayInputs overlayTextInputs{};

    MemoryDebugTraceInputs traceInputs{};
    traceInputs.highWorkTrace = &highWorkTrace;
    traceInputs.lowWorkTrace = &lowWorkTrace;
    traceInputs.lowWorkTraceDeltas = CaptureLowWorkTraceDeltaInputs(trackSystem);

    return BuildMemoryDebugPresentationBundle(
        memorySnapshot,
        overlayPacketInputs,
        overlayTextInputs,
        traceInputs);
}

} // namespace GameLoopMemoryPresentationDomain
