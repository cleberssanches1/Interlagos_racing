#pragma once

#include <algorithm>
#include <cstdint>

#include "interfaces.hpp"

namespace Game
{
// Initial audio event mapper used before full sound driver integration.
class SimpleAudioEvents final : public IAudioEvents
{
public:
    struct Snapshot
    {
        bool engineActive = false;
        bool accelPulse = false;
        bool brakePulse = false;
        bool checkpointPulse = false;
        bool respawnPulse = false;
        uint8_t throttleBand = 0;
        uint8_t rpmProxy = 0;
        uint32_t frameId = 0;
    };

    void OnFrame(const GameplayFrameState& frameState) override
    {
        Snapshot next{};
        next.frameId = frameState.frameId;
        next.engineActive =
            frameState.phase == GameplayFrameState::RacePhase::Running && frameState.wheelsSpinning;
        next.throttleBand = QuantizeThrottle(frameState.throttle);
        next.rpmProxy = BuildRpmProxy(frameState.throttle, frameState.wheelsSpinning);

        if (next.throttleBand > previous_.throttleBand)
        {
            next.accelPulse = true;
            ++accelPulses_;
        }
        if (frameState.braking && !previousBraking_)
        {
            next.brakePulse = true;
            ++brakePulses_;
        }
        if (frameState.checkpointsPassed > previousCheckpoints_)
        {
            next.checkpointPulse = true;
            ++checkpointPulses_;
        }
        if (previousPhase_ == GameplayFrameState::RacePhase::Running &&
            frameState.phase == GameplayFrameState::RacePhase::Idle)
        {
            next.respawnPulse = true;
            ++respawnPulses_;
        }

        previous_ = next;
        previousBraking_ = frameState.braking;
        previousCheckpoints_ = frameState.checkpointsPassed;
        previousPhase_ = frameState.phase;
    }

    const Snapshot& LastSnapshot() const { return previous_; }
    uint32_t AccelPulses() const { return accelPulses_; }
    uint32_t BrakePulses() const { return brakePulses_; }
    uint32_t CheckpointPulses() const { return checkpointPulses_; }
    uint32_t RespawnPulses() const { return respawnPulses_; }

private:
    static uint8_t QuantizeThrottle(int16_t throttle)
    {
        const int16_t clamped = std::clamp<int16_t>(throttle, static_cast<int16_t>(0), static_cast<int16_t>(100));
        return static_cast<uint8_t>(clamped / 20);
    }

    static uint8_t BuildRpmProxy(int16_t throttle, bool wheelsSpinning)
    {
        if (!wheelsSpinning) return 0;
        const int16_t clamped = std::clamp<int16_t>(throttle, static_cast<int16_t>(0), static_cast<int16_t>(100));
        const int16_t rpm = static_cast<int16_t>(24 + (clamped * 2));
        return static_cast<uint8_t>(std::clamp<int16_t>(rpm, static_cast<int16_t>(0), static_cast<int16_t>(255)));
    }

    Snapshot previous_{};
    bool previousBraking_ = false;
    uint32_t previousCheckpoints_ = 0;
    GameplayFrameState::RacePhase previousPhase_ = GameplayFrameState::RacePhase::Idle;
    uint32_t accelPulses_ = 0;
    uint32_t brakePulses_ = 0;
    uint32_t checkpointPulses_ = 0;
    uint32_t respawnPulses_ = 0;
};
} // namespace Game
