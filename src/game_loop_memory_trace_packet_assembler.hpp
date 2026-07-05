#pragma once

#include "game_loop_memory_presentation_state_assembler.hpp"
#include "track_system.hpp"

namespace GameLoopMemoryPresentationDomain
{

struct LowWorkTraceDeltaInputs
{
    int32_t trackDrawPrepareDelta = 0;
    int32_t trackDrawExecuteDelta = 0;
    int32_t trackDrawOtherDelta = 0;
    int32_t trackDrawFrameDelta = 0;
};

inline LowWorkTraceDeltaInputs CaptureLowWorkTraceDeltaInputs(const TrackSystem* trackSystem)
{
    LowWorkTraceDeltaInputs inputs{};
    if (trackSystem == nullptr)
    {
        return inputs;
    }

    inputs.trackDrawPrepareDelta = trackSystem->LowWorkDrawPrepareDeltaThisFrame();
    inputs.trackDrawExecuteDelta = trackSystem->LowWorkDrawExecuteDeltaThisFrame();
    inputs.trackDrawOtherDelta = trackSystem->LowWorkDrawOtherDeltaThisFrame();
    inputs.trackDrawFrameDelta = trackSystem->LowWorkDrawFrameDeltaThisFrame();
    return inputs;
}

inline void SeedHighWorkTraceDeltaPacket(const HighWorkTracePacket& trace,
                                         HighWorkTraceDeltaPacket& outPacket)
{
    outPacket.valid = trace.valid;
#if defined(SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY) && SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY
    outPacket.allocDelta = trace.postSync.allocCalls - trace.begin.allocCalls;
    outPacket.freeDelta = trace.postSync.freeCalls - trace.begin.freeCalls;
    outPacket.reallocDelta = trace.postSync.reallocCalls - trace.begin.reallocCalls;
    outPacket.failedDelta = trace.postSync.failedAllocCalls - trace.begin.failedAllocCalls;
    outPacket.usedBlocks = trace.postSync.usedBlocks;
    outPacket.freeBlocks = trace.postSync.freeBlocks;
#endif
    outPacket.finishAccum = trace.finishAccum;
    outPacket.syncAccum = trace.syncAccum;
    outPacket.freeBytes = trace.postSync.freeBytes;
}

inline HighWorkTraceDeltaPacket BuildHighWorkTraceDeltaPacket(const HighWorkTracePacket& trace)
{
    HighWorkTraceDeltaPacket packet{};
    SeedHighWorkTraceDeltaPacket(trace, packet);
    return packet;
}

inline void SeedLowWorkTraceDeltaPacket(const LowWorkTracePacket& trace,
                                        const LowWorkTraceDeltaInputs& inputs,
                                        LowWorkTraceDeltaPacket& outPacket)
{
    outPacket.valid = trace.valid;
    outPacket.gameplayFreeDelta = GameLoopRuntime::SnapshotFreeDelta(trace.begin, trace.gameplay);
    outPacket.autoLapFreeDelta = GameLoopRuntime::SnapshotFreeDelta(trace.gameplay, trace.autoLap);
    outPacket.backgroundFreeDelta =
        GameLoopRuntime::SnapshotFreeDelta(trace.autoLap, trace.background);
    outPacket.hudFreeDelta = GameLoopRuntime::SnapshotFreeDelta(trace.background, trace.hud);
    outPacket.trackDrawFreeDelta = GameLoopRuntime::SnapshotFreeDelta(trace.hud, trace.trackDraw);
    outPacket.trackEndFreeDelta =
        GameLoopRuntime::SnapshotFreeDelta(trace.trackDraw, trace.trackEnd);
    outPacket.carFreeDelta = GameLoopRuntime::SnapshotFreeDelta(trace.trackEnd, trace.car);
    outPacket.finishFreeDelta = GameLoopRuntime::SnapshotFreeDelta(trace.car, trace.preSync);
    outPacket.syncFreeDelta = GameLoopRuntime::SnapshotFreeDelta(trace.preSync, trace.postSync);
    outPacket.frameFreeDelta = GameLoopRuntime::SnapshotFreeDelta(trace.begin, trace.postSync);
    outPacket.framePayloadDelta = GameLoopRuntime::SnapshotPayloadDelta(trace.begin, trace.postSync);
    outPacket.frameOverheadDelta = GameLoopRuntime::SnapshotOverheadDelta(trace.begin, trace.postSync);
#if defined(SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY) && SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY
    outPacket.largestFreeDelta =
        static_cast<int32_t>(trace.postSync.largestFreeBytes) -
        static_cast<int32_t>(trace.begin.largestFreeBytes);
    outPacket.freeBlocksDelta =
        static_cast<int32_t>(trace.postSync.freeBlocks) -
        static_cast<int32_t>(trace.begin.freeBlocks);
#endif
    outPacket.trackDrawPrepareDelta = inputs.trackDrawPrepareDelta;
    outPacket.trackDrawExecuteDelta = inputs.trackDrawExecuteDelta;
    outPacket.trackDrawOtherDelta = inputs.trackDrawOtherDelta;
    outPacket.trackDrawFrameDelta = inputs.trackDrawFrameDelta;
}

inline LowWorkTraceDeltaPacket BuildLowWorkTraceDeltaPacket(const LowWorkTracePacket& trace,
                                                            const LowWorkTraceDeltaInputs& inputs)
{
    LowWorkTraceDeltaPacket packet{};
    SeedLowWorkTraceDeltaPacket(trace, inputs, packet);
    return packet;
}

inline HighWorkTraceDeltaPacket BuildHighWorkTraceDeltaPacket(const GameLoopRuntime::HwrStageTrace& trace)
{
    return BuildHighWorkTraceDeltaPacket(BuildHighWorkTracePacket(trace));
}

inline LowWorkTraceDeltaPacket BuildLowWorkTraceDeltaPacket(const GameLoopRuntime::LwrStageTrace& trace,
                                                            const LowWorkTraceDeltaInputs& inputs)
{
    return BuildLowWorkTraceDeltaPacket(BuildLowWorkTracePacket(trace), inputs);
}

} // namespace GameLoopMemoryPresentationDomain
