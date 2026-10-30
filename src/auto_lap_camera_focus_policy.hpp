#pragma once

#include <cstdint>

// Camera ownership and track-belt ownership must make the same decision.
// Residency is only an entry condition, not a reason to drop an active focus:
// the focused car is precisely what moves the render window next.
namespace AutoLapCameraFocusPolicy
{
struct Anchor
{
    bool npc = false;
    int32_t segmentId = -1;
};

inline bool MaySwitch(bool targetValid, bool targetResident)
{
    return targetValid && targetResident;
}

inline bool NeedsRecoveryHandoff(bool recoveryPublished, bool playerFocused,
                                bool transferPending, int32_t playerSegmentId,
                                bool playerResident)
{
    return recoveryPublished && playerFocused && !transferPending &&
           playerSegmentId > 0 && !playerResident;
}

inline Anchor Select(bool npcRequested, bool npcValid,
                     int32_t playerSegmentId, int32_t npcSegmentId)
{
    if (npcRequested && npcValid && npcSegmentId > 0)
        return {true, npcSegmentId};
    return {false, playerSegmentId};
}
}
