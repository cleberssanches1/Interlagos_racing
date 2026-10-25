#pragma once

#include <cstdint>

namespace Game::CarTireAudioPolicy
{

// Digital braking has no pressure value. Require a fast approach and measured
// deceleration instead of treating throttle-driven wheel animation as slip.
static constexpr int16_t kEntrySpeedKmh = 100;
static constexpr int16_t kReleaseSpeedKmh = 70;
static constexpr int16_t kEntryDropTwoFramesKmh = 5;
static constexpr int16_t kContinueDropTwoFramesKmh = 3;
static constexpr uint8_t kWeakDecelGraceFrames = 3u;

struct Input
{
    int16_t speedKmh = 0;
    int16_t gear = 0;
    uint8_t brakeHoldFrames = 0u;
    bool braking = false;
};

struct State
{
    int16_t previousSpeedKmh = 0;
    int16_t twoFramesAgoSpeedKmh = 0;
    int16_t brakeEntrySpeedKmh = 0;
    uint8_t previousBrakeHoldFrames = 0u;
    uint8_t weakDecelFrames = 0u;
    bool speedHistoryValid = false;
    bool brakeSequence = false;
    bool triggeredThisBrake = false;
    bool active = false;
};

struct Decision
{
    bool active = false;
    bool started = false;
    bool stopped = false;
};

inline Decision Advance(State& state, const Input& input)
{
    const int16_t speedKmh = (input.speedKmh > 0) ? input.speedKmh : 0;
    if (!state.speedHistoryValid)
    {
        state.previousSpeedKmh = speedKmh;
        state.twoFramesAgoSpeedKmh = speedKmh;
        state.speedHistoryValid = true;
    }

    const bool wasActive = state.active;
    const bool brakingForward =
        input.braking && (input.brakeHoldFrames > 0u) && (input.gear > 0);
    if (!brakingForward)
    {
        state.active = false;
        state.brakeSequence = false;
        state.triggeredThisBrake = false;
        state.weakDecelFrames = 0u;
    }
    else
    {
        if (!state.brakeSequence ||
            (input.brakeHoldFrames < state.previousBrakeHoldFrames))
        {
            state.brakeSequence = true;
            state.triggeredThisBrake = false;
            state.weakDecelFrames = 0u;
            state.brakeEntrySpeedKmh =
                (state.previousSpeedKmh > speedKmh)
                    ? state.previousSpeedKmh : speedKmh;
        }

        const int32_t dropTwoFramesKmh =
            static_cast<int32_t>(state.twoFramesAgoSpeedKmh) - speedKmh;
        if (state.active)
        {
            if (speedKmh < kReleaseSpeedKmh)
            {
                state.active = false;
            }
            else if (dropTwoFramesKmh < kContinueDropTwoFramesKmh)
            {
                if (state.weakDecelFrames < kWeakDecelGraceFrames)
                {
                    ++state.weakDecelFrames;
                }
                if (state.weakDecelFrames >= kWeakDecelGraceFrames)
                {
                    state.active = false;
                }
            }
            else
            {
                state.weakDecelFrames = 0u;
            }
        }
        else if (!state.triggeredThisBrake &&
                 (input.brakeHoldFrames >= 2u) &&
                 (state.brakeEntrySpeedKmh >= kEntrySpeedKmh) &&
                 (speedKmh >= kReleaseSpeedKmh) &&
                 (dropTwoFramesKmh >= kEntryDropTwoFramesKmh))
        {
            state.active = true;
            state.triggeredThisBrake = true;
        }
    }

    state.previousBrakeHoldFrames = input.brakeHoldFrames;
    state.twoFramesAgoSpeedKmh = state.previousSpeedKmh;
    state.previousSpeedKmh = speedKmh;
    return {state.active, state.active && !wasActive, wasActive && !state.active};
}

} // namespace Game::CarTireAudioPolicy
