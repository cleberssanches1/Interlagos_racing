#include <cassert>
#include "auto_lap_camera_focus_policy.hpp"

int main()
{
    using namespace AutoLapCameraFocusPolicy;
    assert(MaySwitch(true, true));
    assert(!MaySwitch(true, false));
    assert(!MaySwitch(false, true));

    assert(NeedsRecoveryHandoff(true, true, false, 25, false));
    assert(!NeedsRecoveryHandoff(false, true, false, 25, false));
    assert(!NeedsRecoveryHandoff(true, false, false, 25, false));
    assert(!NeedsRecoveryHandoff(true, true, true, 25, false));
    assert(!NeedsRecoveryHandoff(true, true, false, -1, false));
    assert(!NeedsRecoveryHandoff(true, true, false, 25, true));

    const Anchor player = Select(false, true, 12, 18);
    assert(!player.npc && player.segmentId == 12);
    // Leaving the old render window must not revoke an already-selected NPC.
    const Anchor npc = Select(true, true, 12, 19);
    assert(npc.npc && npc.segmentId == 19);
    const Anchor lost = Select(true, false, 12, -1);
    assert(!lost.npc && lost.segmentId == 12);
    return 0;
}
