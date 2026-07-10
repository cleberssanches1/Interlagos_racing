#pragma once

#include <srl.hpp>

#include "game_loop_presenter_boundary_text_contracts.hpp"

namespace GameLoopRuntime
{

inline void PresentPresenterBoundaryHudStatusTextPacket(
    const PresenterBoundaryStatusTextPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    SRL::Debug::Print(
        0,
        12,
        "KM/H:%d GEAR:%c RPM:%d    ",
        static_cast<int>(packet.speedKmh),
        packet.gearChar,
        static_cast<int>(packet.engineRpm));
}

} // namespace GameLoopRuntime
