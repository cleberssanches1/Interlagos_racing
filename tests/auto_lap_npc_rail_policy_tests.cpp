#include <cassert>

#include "auto_lap_npc_rail_policy.hpp"

int main()
{
    using AutoLapNpcRailPolicy::GuideLineIndexForNpcSlot;
    using AutoLapNpcRailPolicy::IsNonZeroRouteXZ;
    using AutoLapNpcRailPolicy::NpcSlotForGuideLineIndex;
    using AutoLapNpcRailPolicy::kLeftGuideLineIndex;
    using AutoLapNpcRailPolicy::kNpcRailCount;
    using AutoLapNpcRailPolicy::kRightGuideLineIndex;

    // Current VDP1 budget permits one NPC in addition to the player.
    assert(kNpcRailCount == 1u);
    assert(GuideLineIndexForNpcSlot(0) == kLeftGuideLineIndex);
    assert(GuideLineIndexForNpcSlot(1) == -1);
    assert(GuideLineIndexForNpcSlot(2) == -1);
    assert(NpcSlotForGuideLineIndex(0) == 0);
    assert(NpcSlotForGuideLineIndex(kRightGuideLineIndex) == -1);
    assert(NpcSlotForGuideLineIndex(1) == -1);

    using AutoLapNpcRailPolicy::ShouldAdvanceForSimulationFrame;
    assert(!ShouldAdvanceForSimulationFrame(false, 5u, false, 0u));
    assert(ShouldAdvanceForSimulationFrame(true, 5u, false, 0u));
    assert(!ShouldAdvanceForSimulationFrame(true, 5u, true, 5u));
    assert(ShouldAdvanceForSimulationFrame(true, 6u, true, 5u));
    assert(ShouldAdvanceForSimulationFrame(true, 0u, true, 0xFFFFFFFFu));

    // Speed matching preserves a local gap without snapping route progress.
    assert(AutoLapNpcRailPolicy::StepUnitsForNpc(66, 1) == 12);
    assert(AutoLapNpcRailPolicy::StepUnitsForNpc(66, -3) > 12);
    assert(AutoLapNpcRailPolicy::StepUnitsForNpc(66, 5) < 12);
    assert(AutoLapNpcRailPolicy::StepUnitsForNpc(0, 5) == 0);

    assert(!IsNonZeroRouteXZ(0, 0));
    assert(IsNonZeroRouteXZ(5303 << 16, 0));
    assert(IsNonZeroRouteXZ(0, 42 << 16));
    assert(IsNonZeroRouteXZ(5303 << 16, 1717 << 16));

    assert(AutoLapNpcRailPolicy::StartFail::kOk == 0u);
    assert(AutoLapNpcRailPolicy::StartFail::kCarOrTrack == 1u);
    assert(AutoLapNpcRailPolicy::StartFail::kRouteReady == 2u);
    assert(AutoLapNpcRailPolicy::StartFail::kRouteData == 3u);
    assert(AutoLapNpcRailPolicy::StartFail::kNearestPoint == 4u);
    assert(AutoLapNpcRailPolicy::StartFail::kAsphaltTcol == 5u);

    return 0;
}
