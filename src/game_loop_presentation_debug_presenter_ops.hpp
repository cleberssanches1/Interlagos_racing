#pragma once

#include <srl.hpp>

#include "game_loop_presentation_debug_contracts.hpp"

namespace GameLoopRuntime
{

inline void PresentDrivingHudShiftTextPacket(const DrivingHudTextPacket& packet)
{
    if (packet.shiftFrames > 0u)
    {
        SRL::Debug::Print(
            0,
            11,
            "SHIFT %d>%d f:%u   ",
            static_cast<int>(packet.shiftRpmBefore),
            static_cast<int>(packet.shiftRpmAfter),
            static_cast<unsigned>(packet.shiftFrames));
    }
    else
    {
        SRL::Debug::Print(0, 11, "                         ");
    }
}

} // namespace GameLoopRuntime
