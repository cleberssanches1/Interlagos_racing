#include <cassert>
#include "speedometer_policy.hpp"

int main()
{
    using SpeedometerPolicy::Resolve;
    assert(Resolve(0, 4200, 'N').speedKmh == 0);
    assert(Resolve(19, 6000, '2').gear == '2');
    assert(Resolve(-48, 6800, 'R').speedKmh == 48);
    assert(Resolve(300, 12500, '8').rpmSegments == 12);
    assert(Resolve(1000, 13500, '8').speedKmh == 999);
    assert(Resolve(45, 2000, '?').gear == 'N');
    assert(Resolve(45, 2000, '?').rpmSegments == 0);
    return 0;
}
