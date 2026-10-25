#include <cassert>

#include "time_attack_session.hpp"

int main()
{
    using namespace TimeAttack;
    Config config{};
    config.laps = 3u;
    config.segmentCount = 290u;
    config.startSegment = 13u;
    uint32_t record = 0u;
    Session session{};
    session.Start(config, 100u, &record);
    assert(session.BlocksControls());
    assert(session.CountdownNumber(100u) == 3u);
    session.Tick(279u);
    assert(session.CurrentPhase() == Phase::Countdown);
    session.Tick(280u);
    assert(session.CurrentPhase() == Phase::OutLap);
    assert(!session.BlocksControls());

    // A car placed on the line cannot start by reversing one segment.
    Session lineOscillation{};
    lineOscillation.Start(config, 0u, nullptr);
    lineOscillation.Tick(180u);
    lineOscillation.Observe(1u, 13, 181u);
    lineOscillation.Observe(2u, 12, 182u);
    lineOscillation.Observe(3u, 13, 183u);
    assert(lineOscillation.CurrentPhase() == Phase::OutLap);

    uint32_t frame = 1u;
    uint32_t tick = 280u;
    auto observe = [&](int segment)
    {
        tick += 2u;
        session.Observe(frame++, segment, tick);
    };
    for (int segment = 1; segment <= 13; ++segment) observe(segment);
    assert(session.CurrentPhase() == Phase::Running);
    assert(session.LapsCompleted() == 0u);
    const uint32_t firstStart = tick;

    // Crossing backwards, then returning to the gate, must not count a lap.
    observe(12);
    observe(13);
    assert(session.LapsCompleted() == 0u);
    for (int lap = 0; lap < 3; ++lap)
    {
        for (int segment = 14; segment <= 290; ++segment) observe(segment);
        for (int segment = 1; segment <= 13; ++segment) observe(segment);
        assert(session.LapsCompleted() == lap + 1);
    }
    assert(session.CurrentPhase() == Phase::Finished);
    assert(session.BlocksControls());
    assert(session.LapTicks(0u) >= 580u);
    assert(session.BestLapTicks() < record);
    assert(record == session.TotalTicks(tick));
    assert(session.TotalTicks(tick + 100u) == session.TotalTicks(tick));
    assert(session.TotalTicks(tick) > tick - firstStart);

    // Duplicate async pose publication cannot advance the lap state.
    const uint8_t completed = session.LapsCompleted();
    session.Observe(frame - 1u, 13, tick + 1u);
    assert(session.LapsCompleted() == completed);

    // A fresh session retains the record through its external RAM slot.
    Session next{};
    next.Start(config, tick + 200u, &record);
    assert(next.RecordCourseTicks() == session.TotalTicks(tick));
    return 0;
}
