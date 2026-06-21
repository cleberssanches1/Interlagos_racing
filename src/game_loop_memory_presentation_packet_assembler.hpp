#pragma once

#include "game_loop_memory_presentation_state_assembler.hpp"
#include "game_loop_observability_state_assembler.hpp"

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

} // namespace GameLoopMemoryPresentationDomain
