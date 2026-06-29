#pragma once

#include <srl.hpp>

#include "game_loop_memory_debug_contracts.hpp"
#include "game_loop_memory_trace_text_low_work_view_assembler.hpp"
#include "game_loop_memory_trace_text_view_assembler.hpp"

namespace GameLoopMemoryPresentationDomain
{

inline void PresentWorkRamUsagePacket(const WorkRamUsagePacket& packet)
{
    SRL::Debug::Print(2, 14, "HW u:%u f:%u     ",
                      static_cast<unsigned>(packet.highWorkUsed),
                      static_cast<unsigned>(packet.highWorkFree));
    SRL::Debug::Print(2, 15, "LW u:%u f:%u     ",
                      static_cast<unsigned>(packet.lowWorkUsed),
                      static_cast<unsigned>(packet.lowWorkFree));
}

inline void PresentHighWorkTraceTextViewPacket(const HighWorkTraceTextViewPacket& packet)
{
    SRL::Debug::Print(2, 18, "H3 a:%u f:%u r:%u x:%u ub:%u fb:%u ",
                      static_cast<unsigned>(packet.allocDelta),
                      static_cast<unsigned>(packet.freeDelta),
                      static_cast<unsigned>(packet.reallocDelta),
                      static_cast<unsigned>(packet.failedDelta),
                      static_cast<unsigned>(packet.usedBlocks),
                      static_cast<unsigned>(packet.freeBlocks));
    SRL::Debug::Print(2, 19, "H4 fn:%d sy:%d fr:%u   ",
                      packet.finishAccum,
                      packet.syncAccum,
                      static_cast<unsigned>(packet.freeBytes));
}

inline void PresentLowWorkTraceTextViewPacket(const LowWorkTraceTextViewPacket& packet)
{
    SRL::Debug::Print(2, 26, "L1 gp:%d au:%d bg:%d hd:%d ",
                      packet.gameplayFreeDelta,
                      packet.autoLapFreeDelta,
                      packet.backgroundFreeDelta,
                      packet.hudFreeDelta);
    SRL::Debug::Print(2, 27, "L2 td:%d te:%d c:%d f:%d ",
                      packet.trackDrawFreeDelta,
                      packet.trackEndFreeDelta,
                      packet.carFreeDelta,
                      packet.finishFreeDelta);
    SRL::Debug::Print(2, 28, "L3 sy:%d fr:%d py:%d ov:%d ",
                      packet.syncFreeDelta,
                      packet.frameFreeDelta,
                      packet.framePayloadDelta,
                      packet.frameOverheadDelta);
    SRL::Debug::Print(2, 29, "L4 dp:%d dx:%d do:%d df:%d ",
                      packet.trackDrawPrepareDelta,
                      packet.trackDrawExecuteDelta,
                      packet.trackDrawOtherDelta,
                      packet.trackDrawFrameDelta);
    SRL::Debug::Print(2, 30, "L5 lf:%d fb:%d      ",
                      packet.largestFreeDelta,
                      packet.freeBlocksDelta);
}

inline void PresentMemoryDebugPresentationBundle(const MemoryDebugPresentationBundle& bundle)
{
    if (bundle.memoryFlow.workRamUsage.valid)
    {
        PresentWorkRamUsagePacket(bundle.memoryFlow.workRamUsage);
    }
    if (bundle.highTraceText.valid)
    {
        PresentHighWorkTraceTextViewPacket(BuildHighWorkTraceTextViewPacket(bundle.highTraceText));
    }
    if (bundle.lowTraceText.valid)
    {
        PresentLowWorkTraceTextViewPacket(BuildLowWorkTraceTextViewPacket(bundle.lowTraceText));
    }
}

} // namespace GameLoopMemoryPresentationDomain
