#include <cassert>

#include "auto_lap_route_selection_policy.hpp"

int main()
{
    using AutoLapRouteSelectionPolicy::Select;

    // Auto Lap always follows the middle PATH.NYA line, not the one nearest
    // the initial car position.
    assert(Select(true, true, 2) == 1);
    assert(Select(true, true, 0) == 1);
    assert(Select(true, false, 2) == -1);

    // Other PATH consumers retain their original nearest-line choice.
    assert(Select(false, true, 2) == 2);
    assert(Select(false, false, 0) == 0);
    return 0;
}
