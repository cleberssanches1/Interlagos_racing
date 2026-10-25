#include <iostream>

#include "car_tire_audio_policy.hpp"

namespace
{
int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { \
    std::cerr << __LINE__ << ": expected " #expr "\n"; ++failures; } } while (0)

Game::CarTireAudioPolicy::Decision Step(
    Game::CarTireAudioPolicy::State& state,
    int16_t speedKmh,
    uint8_t brakeFrames = 0u,
    bool braking = false,
    int16_t gear = 2)
{
    return Game::CarTireAudioPolicy::Advance(
        state, {speedKmh, gear, brakeFrames, braking});
}

void TestAccelerationNeverStartsSkid()
{
    Game::CarTireAudioPolicy::State state{};
    for (int16_t speed = 0; speed <= 200; speed += 10)
    {
        const auto decision = Step(state, speed);
        EXPECT_TRUE(!decision.active && !decision.started);
    }
    EXPECT_TRUE(!Step(state, 205).active);
}

void TestShortAndLowSpeedBrakingStaySilent()
{
    Game::CarTireAudioPolicy::State state{};
    Step(state, 140);
    EXPECT_TRUE(!Step(state, 137, 1u, true).active);
    EXPECT_TRUE(!Step(state, 137).active);
    Step(state, 95);
    EXPECT_TRUE(!Step(state, 92, 1u, true).active);
    EXPECT_TRUE(!Step(state, 89, 2u, true).active);
    EXPECT_TRUE(!Step(state, 86, 3u, true).active);
}

void TestWeakBrakingStaysSilent()
{
    Game::CarTireAudioPolicy::State state{};
    Step(state, 140);
    EXPECT_TRUE(!Step(state, 139, 1u, true).active);
    EXPECT_TRUE(!Step(state, 138, 2u, true).active);
    EXPECT_TRUE(!Step(state, 137, 3u, true).active);
    EXPECT_TRUE(!Step(state, 136, 4u, true).active);
}

void TestHardBrakingStartsOnceAndStopsOnRelease()
{
    Game::CarTireAudioPolicy::State state{};
    Step(state, 140);
    EXPECT_TRUE(!Step(state, 137, 1u, true).active);
    const auto start = Step(state, 134, 2u, true);
    EXPECT_TRUE(start.active && start.started && !start.stopped);
    const auto held = Step(state, 131, 3u, true);
    EXPECT_TRUE(held.active && !held.started && !held.stopped);
    const auto released = Step(state, 131);
    EXPECT_TRUE(!released.active && !released.started && released.stopped);
    EXPECT_TRUE(!Step(state, 135).active);
}

void TestNoReverseOrReleasedBrakeTrigger()
{
    Game::CarTireAudioPolicy::State state{};
    Step(state, 140);
    EXPECT_TRUE(!Step(state, 137, 1u, true, -1).active);
    EXPECT_TRUE(!Step(state, 134, 2u, true, -1).active);
    EXPECT_TRUE(!Step(state, 131, 0u, true).active);
}

void TestSkidEndsWithoutDecelerationAndDoesNotRetrigger()
{
    Game::CarTireAudioPolicy::State state{};
    Step(state, 140);
    Step(state, 137, 1u, true);
    EXPECT_TRUE(Step(state, 134, 2u, true).started);
    EXPECT_TRUE(Step(state, 134, 3u, true).active);
    EXPECT_TRUE(Step(state, 134, 4u, true).active);
    EXPECT_TRUE(Step(state, 134, 5u, true).active);
    const auto stopped = Step(state, 134, 6u, true);
    EXPECT_TRUE(stopped.stopped && !stopped.active);
    EXPECT_TRUE(!Step(state, 128, 7u, true).started);
    Step(state, 140);
    Step(state, 137, 1u, true);
    EXPECT_TRUE(Step(state, 134, 2u, true).started);
}

void TestSkidEndsAtLowSpeed()
{
    Game::CarTireAudioPolicy::State state{};
    Step(state, 103);
    Step(state, 100, 1u, true);
    EXPECT_TRUE(Step(state, 97, 2u, true).started);
    const auto slow = Step(state, 69, 3u, true);
    EXPECT_TRUE(slow.stopped && !slow.active);
}
} // namespace

int main()
{
    TestAccelerationNeverStartsSkid();
    TestShortAndLowSpeedBrakingStaySilent();
    TestWeakBrakingStaysSilent();
    TestHardBrakingStartsOnceAndStopsOnRelease();
    TestNoReverseOrReleasedBrakeTrigger();
    TestSkidEndsWithoutDecelerationAndDoesNotRetrigger();
    TestSkidEndsAtLowSpeed();
    if (failures != 0)
    {
        std::cerr << "car tire audio policy: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "car tire audio policy: ok\n";
    return 0;
}
