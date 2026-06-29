#pragma once

#include <srl.hpp>

#include "game_loop_memory_overlay_text_contracts.hpp"
#include "game_loop_memory_overlay_text_view_contracts.hpp"

namespace GameLoopMemoryPresentationDomain
{

inline void PresentLowWorkOverlayTextViewPacket(const LowWorkOverlayTextViewPacket& packet)
{
    SRL::Debug::Print(2, 15, "LW f:%u d:%d s:%u i:%d ",
                      static_cast<unsigned>(packet.lowWorkFree),
                      static_cast<int>(packet.freeDelta),
                      static_cast<unsigned>(packet.slides),
                      static_cast<int>(packet.slideId));
}

inline void PresentHighWorkOverlayTextPacket(const HighWorkOverlayTextPacket& packet)
{
    SRL::Debug::Print(2, 12, "HW1 i:%u g:%u u:%u ",
                      static_cast<unsigned>(packet.initUnknown),
                      static_cast<unsigned>(packet.gameplayAuto),
                      static_cast<unsigned>(packet.ui));
    SRL::Debug::Print(2, 13, "HW2 t:%u c:%u fs:%u hf:%u",
                      static_cast<unsigned>(packet.track),
                      static_cast<unsigned>(packet.car),
                      static_cast<unsigned>(packet.finishSync),
                      static_cast<unsigned>(packet.freeBytes));
}

inline void PresentLowWorkOverlayBreakdownTextPacket(const LowWorkOverlayBreakdownTextPacket& packet)
{
    SRL::Debug::Print(2, 16, "LC1 r:%u s:%u w:%u ",
                      static_cast<unsigned>(packet.renderers),
                      static_cast<unsigned>(packet.slotState),
                      static_cast<unsigned>(packet.workingSet));
    SRL::Debug::Print(2, 17, "LC2 f:%u t:%u m:%u ",
                      static_cast<unsigned>(packet.familyCache),
                      static_cast<unsigned>(packet.transient),
                      static_cast<unsigned>(packet.metadata));
}

inline void PresentLowWorkOverlayTicksCompact(const LowWorkOverlayTicksTextPacket& packet,
                                              const LowWorkOverlayTextViewPacket& viewPacket)
{
    SRL::Debug::Print(2, 18, "TK1 st:%u mw:%u dr:%u fr:%u ",
                      static_cast<unsigned>(packet.trackStreamTicks),
                      static_cast<unsigned>(packet.trackMaintenanceTicks),
                      static_cast<unsigned>(packet.trackDrawTicks),
                      static_cast<unsigned>(packet.trackFrameTicks));
    SRL::Debug::Print(2, 19, "TK2 w:%u pf:%u ld:%u ws:%u ",
                      static_cast<unsigned>(packet.trackWindowTicks),
                      static_cast<unsigned>(packet.trackPrefetchTicks),
                      static_cast<unsigned>(packet.trackLodTicks),
                      static_cast<unsigned>(packet.trackWorkingSetTicks));
    SRL::Debug::Print(2, 20, "PB b:%u/%u d:%u   ",
                      static_cast<unsigned>(viewPacket.prefetchBuildAttempts),
                      static_cast<unsigned>(viewPacket.prefetchBuildBudget),
                      static_cast<unsigned>(viewPacket.prefetchBuildDrops));
}

inline void PresentLowWorkTagGroupTextPacket(const LowWorkTagGroupTextPacket& packet)
{
    SRL::Debug::Print(2, 20, "TX1 i:%u g:%u u:%u ",
                      static_cast<unsigned>(packet.initUnknown),
                      static_cast<unsigned>(packet.gameplayAuto),
                      static_cast<unsigned>(packet.ui));
    SRL::Debug::Print(2, 21, "TX2 t:%u c:%u fs:%u ",
                      static_cast<unsigned>(packet.track),
                      static_cast<unsigned>(packet.car),
                      static_cast<unsigned>(packet.finishSync));
}

inline void PresentLowWorkAllocatorTextPacket(const LowWorkAllocatorTextPacket& packet)
{
    SRL::Debug::Print(2, 22, "FO1 py:%u ov:%u fb:%u ",
                      static_cast<unsigned>(packet.payloadBytes),
                      static_cast<unsigned>(packet.overheadBytes),
                      static_cast<unsigned>(packet.freeBlocks));
    SRL::Debug::Print(2, 23, "FO2 kn:%u iv:%u ib:%u ",
                      static_cast<unsigned>(packet.knownTaggedBytes),
                      static_cast<unsigned>(packet.invalidTaggedBytes),
                      static_cast<unsigned>(packet.invalidTaggedBlocks));
}

inline void PresentLowWorkOverlayTicksFull(const LowWorkOverlayTicksTextPacket& packet)
{
    SRL::Debug::Print(2, 24, "TK1 st:%u mw:%u dr:%u fr:%u ",
                      static_cast<unsigned>(packet.trackStreamTicks),
                      static_cast<unsigned>(packet.trackMaintenanceTicks),
                      static_cast<unsigned>(packet.trackDrawTicks),
                      static_cast<unsigned>(packet.trackFrameTicks));
    SRL::Debug::Print(2, 25, "TK2 w:%u pf:%u ld:%u ws:%u ",
                      static_cast<unsigned>(packet.trackWindowTicks),
                      static_cast<unsigned>(packet.trackPrefetchTicks),
                      static_cast<unsigned>(packet.trackLodTicks),
                      static_cast<unsigned>(packet.trackWorkingSetTicks));
    SRL::Debug::Print(2, 26, "PB b:%u/%u d:%u   ",
                      static_cast<unsigned>(packet.prefetchBuildAttempts),
                      static_cast<unsigned>(packet.prefetchBuildBudget),
                      static_cast<unsigned>(packet.prefetchBuildDrops));
}

} // namespace GameLoopMemoryPresentationDomain
