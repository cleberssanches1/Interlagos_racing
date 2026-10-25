#pragma once

#include <cstdint>

namespace TimeAttack
{
constexpr uint8_t kMaxLaps = 12u;

enum class Phase : uint8_t { Countdown, OutLap, Running, Finished };

struct Config
{
    uint8_t laps = 3u;
    uint16_t startSegment = 13u;
    uint16_t segmentCount = 290u;
    uint8_t ticksPerSecond = 60u;
};

class Session
{
public:
    void Start(const Config& config, uint32_t now, uint32_t* recordCourseTicks)
    {
        config_ = config;
        if (config_.laps == 0u) config_.laps = 1u;
        if (config_.laps > kMaxLaps) config_.laps = kMaxLaps;
        if (config_.ticksPerSecond == 0u) config_.ticksPerSecond = 60u;
        phase_ = Phase::Countdown;
        countdownStart_ = now;
        greenTick_ = now;
        lapStartTick_ = now;
        finishTick_ = now;
        recordCourseTicks_ = recordCourseTicks;
        completedLaps_ = 0u;
        bestLapTicks_ = 0u;
        maxLapRank_ = 0u;
        outlapForwardSteps_ = 0u;
        previousRank_ = -1;
        lastFrameId_ = 0u;
        haveFrameId_ = false;
        for (uint8_t i = 0u; i < kMaxLaps; ++i) lapTicks_[i] = 0u;
    }

    void Tick(uint32_t now)
    {
        if (phase_ != Phase::Countdown) return;
        if (now - countdownStart_ < static_cast<uint32_t>(3u * config_.ticksPerSecond)) return;
        greenTick_ = now;
        phase_ = Phase::OutLap;
        previousRank_ = -1;
    }

    void Observe(uint32_t frameId, int32_t segmentId, uint32_t now)
    {
        if (phase_ != Phase::OutLap && phase_ != Phase::Running) return;
        if (haveFrameId_ && frameId == lastFrameId_) return;
        haveFrameId_ = true;
        lastFrameId_ = frameId;
        if (segmentId < 1 || segmentId > config_.segmentCount ||
            config_.segmentCount < 4u || config_.startSegment < 1u ||
            config_.startSegment > config_.segmentCount) return;

        const int32_t count = config_.segmentCount;
        const int32_t rank = (segmentId - config_.startSegment + count) % count;
        if (previousRank_ < 0)
        {
            previousRank_ = static_cast<int16_t>(rank);
            return;
        }
        int32_t delta = rank - previousRank_;
        if (delta > count / 2) delta -= count;
        if (delta < -count / 2) delta += count;
        const bool forward = delta > 0 && delta <= 16;
        const bool crossedLine = forward && rank < previousRank_;
        previousRank_ = static_cast<int16_t>(rank);
        if (phase_ == Phase::OutLap && delta < 0) outlapForwardSteps_ = 0u;
        if (!forward) return;

        if (phase_ == Phase::OutLap)
        {
            if (outlapForwardSteps_ < config_.segmentCount)
            {
                const uint32_t next = static_cast<uint32_t>(outlapForwardSteps_) + delta;
                outlapForwardSteps_ = static_cast<uint16_t>(
                    next > config_.segmentCount ? config_.segmentCount : next);
            }
            if (crossedLine && outlapForwardSteps_ >= 4u)
            {
                phase_ = Phase::Running;
                lapStartTick_ = now;
                maxLapRank_ = 0u;
            }
            return;
        }
        if (rank > maxLapRank_) maxLapRank_ = static_cast<uint16_t>(rank);
        // Require nearly the whole route. This rejects oscillating over the
        // line and U-turns, without requiring every collision segment to appear.
        if (!crossedLine || maxLapRank_ < count - 17) return;

        const uint32_t lapDuration = now - lapStartTick_;
        lapTicks_[completedLaps_] = lapDuration;
        ++completedLaps_;
        if (bestLapTicks_ == 0u || lapDuration < bestLapTicks_) bestLapTicks_ = lapDuration;
        lapStartTick_ = now;
        maxLapRank_ = 0u;
        if (completedLaps_ >= config_.laps)
        {
            finishTick_ = now;
            phase_ = Phase::Finished;
            const uint32_t total = finishTick_ - greenTick_;
            if (recordCourseTicks_ && (*recordCourseTicks_ == 0u || total < *recordCourseTicks_))
                *recordCourseTicks_ = total;
        }
    }

    Phase CurrentPhase() const { return phase_; }
    bool BlocksControls() const { return phase_ == Phase::Countdown || phase_ == Phase::Finished; }
    uint8_t LapsTarget() const { return config_.laps; }
    uint8_t LapsCompleted() const { return completedLaps_; }
    uint32_t LapTicks(uint8_t index) const { return index < completedLaps_ ? lapTicks_[index] : 0u; }
    uint32_t BestLapTicks() const { return bestLapTicks_; }
    uint32_t RecordCourseTicks() const { return recordCourseTicks_ ? *recordCourseTicks_ : 0u; }
    uint32_t CurrentLapTicks(uint32_t now) const { return phase_ == Phase::Running ? now - lapStartTick_ : 0u; }
    uint32_t TotalTicks(uint32_t now) const
    {
        return phase_ == Phase::Countdown ? 0u :
               (phase_ == Phase::Finished ? finishTick_ : now) - greenTick_;
    }
    uint8_t CountdownNumber(uint32_t now) const
    {
        if (phase_ != Phase::Countdown) return 0u;
        const uint32_t elapsed = (now - countdownStart_) / config_.ticksPerSecond;
        return elapsed >= 3u ? 1u : static_cast<uint8_t>(3u - elapsed);
    }
    uint8_t TicksPerSecond() const { return config_.ticksPerSecond; }

private:
    Config config_{};
    Phase phase_ = Phase::Countdown;
    uint32_t countdownStart_ = 0u;
    uint32_t greenTick_ = 0u;
    uint32_t lapStartTick_ = 0u;
    uint32_t finishTick_ = 0u;
    uint32_t* recordCourseTicks_ = nullptr;
    uint32_t lapTicks_[kMaxLaps]{};
    uint32_t bestLapTicks_ = 0u;
    uint16_t maxLapRank_ = 0u;
    uint16_t outlapForwardSteps_ = 0u;
    int16_t previousRank_ = -1;
    uint32_t lastFrameId_ = 0u;
    uint8_t completedLaps_ = 0u;
    bool haveFrameId_ = false;
};
} // namespace TimeAttack
