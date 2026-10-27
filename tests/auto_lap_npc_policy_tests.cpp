#include "../src/auto_lap_npc_policy.hpp"

#include <cassert>

int main()
{
    const int32_t lengths[] = {100 << 16, 200 << 16, 300 << 16};
    AutoLapNpcPolicy::State first{};
    AutoLapNpcPolicy::State second{};
    second.segment = 1u;

    assert(AutoLapNpcPolicy::Advance(first, lengths, 3u, 150 << 16) == 1u);
    assert(first.segment == 1u && first.alongRaw == (50 << 16));
    assert(second.segment == 1u && second.alongRaw == 0);
    assert(AutoLapNpcPolicy::Advance(second, lengths, 3u, 500 << 16) == 2u);
    assert(second.segment == 0u && second.alongRaw == 0);
    assert(first.segment == 1u && first.alongRaw == (50 << 16));

    AutoLapNpcPolicy::ApproachSpeed(first, 11 << 16, 4u);
    assert(first.speedRaw == (1 << 16));
    assert(AutoLapNpcPolicy::TargetSpeedRaw(30, 0u) == (5 << 16));
    assert(AutoLapNpcPolicy::TargetSpeedRaw(0, 1u) == (12 << 16));
    assert(AutoLapNpcPolicy::ShouldReverseLine(350, 0) == false);
    assert(AutoLapNpcPolicy::ShouldReverseLine(180, 0));
    assert(AutoLapNpcPolicy::TrackFaceBudget(1700u, 318u, 2u) == 682u);
    assert(AutoLapNpcPolicy::TrackFaceBudget(1700u, 318u, 0u) == 1636u);
    return 0;
}
