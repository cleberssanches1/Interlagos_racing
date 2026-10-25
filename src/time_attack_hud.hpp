#pragma once

#include <srl.hpp>

#include "time_attack_session.hpp"

namespace TimeAttack
{
inline void PrintClock(uint8_t x, uint8_t row, uint32_t ticks, uint8_t hz)
{
    const uint32_t seconds = ticks / hz;
    const uint32_t millis = ((ticks % hz) * 1000u) / hz;
    SRL::Debug::Print(x, row, "%02lu:%02lu.%03lu   ",
                      static_cast<unsigned long>(seconds / 60u),
                      static_cast<unsigned long>(seconds % 60u),
                      static_cast<unsigned long>(millis));
}

// Two timing columns on NBG3, above the minimap and speedometer. No VDP1
// commands or per-frame allocations are needed for the timing display.
inline void Draw(const Session& session, uint32_t now)
{
    const uint8_t hz = session.TicksPerSecond();
    SRL::Debug::Print(18, 0, "TIME ATTACK %u/%u   ",
                      static_cast<unsigned>(session.LapsCompleted()),
                      static_cast<unsigned>(session.LapsTarget()));
    SRL::Debug::Print(1, 1, "TOTAL TIME      ");
    PrintClock(2, 2, session.TotalTicks(now), hz);
    SRL::Debug::Print(1, 4, "LAP TIME        ");

    const uint8_t firstVisible = session.LapsCompleted() > 2u
        ? static_cast<uint8_t>(session.LapsCompleted() - 2u) : 0u;
    for (uint8_t slot = 0u; slot < 3u; ++slot)
    {
        const uint8_t i = static_cast<uint8_t>(firstVisible + slot);
        const uint8_t row = static_cast<uint8_t>(5u + slot);
        if (i >= session.LapsTarget())
        {
            SRL::Debug::Print(1, row, "                    ");
            continue;
        }
        if (i < session.LapsCompleted())
        {
            SRL::Debug::Print(1, row, "%u", static_cast<unsigned>(i + 1u));
            PrintClock(3, row, session.LapTicks(i), hz);
        }
        else if (i == session.LapsCompleted() && session.CurrentPhase() == Phase::Running)
        {
            SRL::Debug::Print(1, row, "%u", static_cast<unsigned>(i + 1u));
            PrintClock(3, row, session.CurrentLapTicks(now), hz);
        }
        else SRL::Debug::Print(1, row, "%u --:--.---        ", static_cast<unsigned>(i + 1u));
    }
    SRL::Debug::Print(26, 1, "BEST LAP      ");
    if (session.BestLapTicks()) PrintClock(27, 2, session.BestLapTicks(), hz);
    else SRL::Debug::Print(27, 2, "--:--.---   ");
    SRL::Debug::Print(24, 4, "COURSE RECORD   ");
    if (session.RecordCourseTicks()) PrintClock(27, 5, session.RecordCourseTicks(), hz);
    else SRL::Debug::Print(27, 5, "--:--.---   ");

    if (session.CurrentPhase() == Phase::Countdown)
        SRL::Debug::Print(17, 10, "START IN %u   ", static_cast<unsigned>(session.CountdownNumber(now)));
    else if (session.CurrentPhase() == Phase::OutLap)
        SRL::Debug::Print(17, 10, session.TotalTicks(now) < hz ? "GO!          " : "OUT LAP      ");
    else if (session.CurrentPhase() == Phase::Finished)
        SRL::Debug::Print(17, 10, "TIME ATTACK END");
    else SRL::Debug::Print(17, 10, "               ");
}
} // namespace TimeAttack
