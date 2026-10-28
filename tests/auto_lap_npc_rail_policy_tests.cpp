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

    assert(kNpcRailCount == 2u);
    assert(GuideLineIndexForNpcSlot(0) == kLeftGuideLineIndex);
    assert(GuideLineIndexForNpcSlot(1) == kRightGuideLineIndex);
    assert(GuideLineIndexForNpcSlot(2) == -1);
    assert(NpcSlotForGuideLineIndex(0) == 0);
    assert(NpcSlotForGuideLineIndex(2) == 1);
    assert(NpcSlotForGuideLineIndex(1) == -1);

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
