#pragma once

#include "game_loop_presentation_debug_contracts.hpp"
#include "game_loop_presenter_boundary_text_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterBoundaryStatusTextPacket(
    const DrivingHudTextPacket& drivingHud,
    PresenterBoundaryStatusTextPacket& outPacket)
{
    outPacket.valid = drivingHud.valid;
    outPacket.gearChar = drivingHud.gearChar;
    outPacket.speedKmh = drivingHud.speedKmh;
    outPacket.engineRpm = drivingHud.engineRpm;
}

inline PresenterBoundaryStatusTextPacket BuildPresenterBoundaryStatusTextPacket(
    const DrivingHudTextPacket& drivingHud)
{
    PresenterBoundaryStatusTextPacket packet{};
    SeedPresenterBoundaryStatusTextPacket(drivingHud, packet);
    return packet;
}

} // namespace GameLoopRuntime
