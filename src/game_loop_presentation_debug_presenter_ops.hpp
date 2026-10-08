#pragma once

#include <srl.hpp>

#include "game_loop_presentation_debug_contracts.hpp"

namespace GameLoopRuntime
{

inline void PresentDrivingHudShiftTextPacket(const DrivingHudTextPacket& packet)
{
    // Avoid a blank Debug::Print every idle frame — that alone feeds the ~4 FPS NBG3 tax.
    static uint16_t s_lastShiftFrames = 0u;
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
    else if (s_lastShiftFrames > 0u)
    {
        SRL::Debug::Print(0, 11, "                         ");
    }
    s_lastShiftFrames = packet.shiftFrames;
}

} // namespace GameLoopRuntime
