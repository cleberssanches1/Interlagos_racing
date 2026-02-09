#pragma once

#include <cstdint>
#include <srl.hpp>

#include "frame_telemetry.hpp"

class SoakMonitor
{
public:
    // Reset monitor for a new long-run validation session.
    void Reset()
    {
        stableFrames_ = 0;
        warningFrames_ = 0;
        timeoutEvents_ = 0;
        consecutiveStable_ = 0;
        maxConsecutiveStable_ = 0;
        lastTimeoutFallbacks_ = 0;
    }

    // Update monitor from per-frame telemetry.
    void Update(bool trackEnabled, const FrameTelemetry& telemetry)
    {
        if (!trackEnabled)
        {
            ++stableFrames_;
            ++consecutiveStable_;
            if (consecutiveStable_ > maxConsecutiveStable_) maxConsecutiveStable_ = consecutiveStable_;
            return;
        }

        const bool timeoutRaised = telemetry.producer.timeoutFallbacks > lastTimeoutFallbacks_;
        if (timeoutRaised)
        {
            ++timeoutEvents_;
        }

        const bool frameWarning =
            timeoutRaised ||
            telemetry.producer.jobInFlight ||
            telemetry.trackSegmentsPrepared == 0;

        if (frameWarning)
        {
            ++warningFrames_;
            consecutiveStable_ = 0;
        }
        else
        {
            ++stableFrames_;
            ++consecutiveStable_;
            if (consecutiveStable_ > maxConsecutiveStable_) maxConsecutiveStable_ = consecutiveStable_;
        }

        lastTimeoutFallbacks_ = telemetry.producer.timeoutFallbacks;
    }

    // Display condensed soak health on debug overlay.
    void Present() const
    {
        SRL::Debug::Print(2, 29, "SOAK ok:%lu warn:%lu tmo:%lu",
                          (unsigned long)stableFrames_,
                          (unsigned long)warningFrames_,
                          (unsigned long)timeoutEvents_);
        SRL::Debug::Print(2, 30, "SOAK run:%lu max:%lu",
                          (unsigned long)consecutiveStable_,
                          (unsigned long)maxConsecutiveStable_);
    }

private:
    uint32_t stableFrames_ = 0;
    uint32_t warningFrames_ = 0;
    uint32_t timeoutEvents_ = 0;
    uint32_t consecutiveStable_ = 0;
    uint32_t maxConsecutiveStable_ = 0;
    uint32_t lastTimeoutFallbacks_ = 0;
};
