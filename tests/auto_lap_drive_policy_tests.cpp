#include <cassert>

#include "auto_lap_drive_policy.hpp"

namespace
{
constexpr int32_t U(int32_t units) { return units * 65536; }
}

int main()
{
    using namespace AutoLapDrivePolicy;
    Input input{};
    input.car = {0, 0};
    input.current = {0, 0};
    input.next = {0, U(-200)};
    input.afterNext = {0, U(-400)};
    input.speedKmh = 0;
    input.groundSupported = true;
    State state{};

    const Output straight = Step(input, state);
    assert(straight.target.xRaw == 0);
    assert(straight.target.zRaw == U(-55));
    assert(straight.steering == 0);
    assert(straight.accelerate && !straight.brake);
    assert(straight.targetSpeedKmh == 82);

    input.next = {U(100), U(-200)};
    input.afterNext = {U(200), U(-400)};
    const Output right = Step(input, state);
    assert(right.steering < 0 && right.steering >= -12);
    input.next = {U(-100), U(-200)};
    input.afterNext = {U(-200), U(-400)};
    const Output left = Step(input, state);
    assert(left.steering > right.steering); // bounded reversal, no full-lock jump

    // Segment 1 starts near (5304, 1717); the middle PATH.NYA line 1 is
    // farther toward decreasing X. Positive steering reaches that line.
    Input spawn{};
    spawn.car = {U(5304), U(1717)};
    spawn.current = {U(5176), U(1717)};
    spawn.next = {U(5180), U(1445)};
    spawn.afterNext = {U(5184), U(1181)};
    spawn.groundSupported = true;
    State spawnState{};
    const Output spawnCommand = Step(spawn, spawnState);
    assert(spawnCommand.target.xRaw < spawn.car.xRaw);
    assert(spawnCommand.steering > 0);

    spawn.car = spawn.current;
    State alignedState{};
    const Output aligned = Step(spawn, alignedState);
    assert(aligned.steering >= -4 && aligned.steering <= 4);

    input.upcomingTurnDeg = 30;
    input.speedKmh = 70;
    input.surfaceType = 3;
    const Output grass = Step(input, state);
    assert(grass.targetSpeedKmh == 28);
    assert(grass.brake && !grass.accelerate);

    input.speedKmh = 0;
    input.wallHit = true;
    const Output blocked = Step(input, state);
    assert(!blocked.accelerate && !blocked.brake);

    input.wallHit = false;
    input.groundSupported = false;
    input.surfaceType = 0;
    for (int i = 0; i < 9; ++i) (void)Step(input, state);
    const Output unsupported = Step(input, state);
    assert(unsupported.targetSpeedKmh == 22);
    assert(!unsupported.accelerate);
    input.groundSupported = true;
    assert(Step(input, state).accelerate);
    return 0;
}
