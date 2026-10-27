#include <cassert>

#include "auto_lap_startup_policy.hpp"

int main()
{
    using AutoLapStartupPolicy::CanUsePathHeight;
    const uint32_t nearDistance = AutoLapStartupPolicy::ManhattanWorldUnits(
        5065 << 16, 1754 << 16, 5303 << 16, 1717 << 16);
    const uint32_t farDistance = AutoLapStartupPolicy::ManhattanWorldUnits(
        9350 << 16, 10347 << 16, 5303 << 16, 1717 << 16);
    assert(nearDistance == 275u);
    assert(farDistance == 12677u);
    assert(nearDistance < farDistance);
    assert(CanUsePathHeight(true, 1, 290, 275ll * 275ll));
    assert(CanUsePathHeight(true, 290, 290, 1024ll * 1024ll));
    assert(!CanUsePathHeight(false, 1, 290, 0));
    assert(!CanUsePathHeight(true, 0, 290, 0));
    assert(!CanUsePathHeight(true, 291, 290, 0));
    assert(!CanUsePathHeight(true, 1, 290, 1025ll * 1025ll));
    return 0;
}
