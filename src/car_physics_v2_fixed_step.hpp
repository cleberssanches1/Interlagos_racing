#pragma once

#include <cstdint>

namespace Game::CarPhysicsV2
{
// Fixed-step scheduler for deterministic physics stepping.
// This initial rollout keeps one render quantum per call and allows
// bounded catch-up to avoid long stalls.
class FixedStepScheduler
{
public:
    struct StepPlan
    {
        int32_t steps = 1;
        int32_t remainderQuanta = 0;
    };

    StepPlan BeginFrame()
    {
        accumulatorQuanta_ += kRenderFrameQuanta;
        int32_t steps = accumulatorQuanta_ / kFixedStepQuanta;
        if (steps < 1) steps = 1;
        if (steps > kMaxCatchUpSteps) steps = kMaxCatchUpSteps;
        accumulatorQuanta_ -= (steps * kFixedStepQuanta);
        if (accumulatorQuanta_ < 0) accumulatorQuanta_ = 0;
        return StepPlan{steps, accumulatorQuanta_};
    }

    void Reset()
    {
        accumulatorQuanta_ = 0;
    }

private:
    static constexpr int32_t kRenderFrameQuanta = 1;
    static constexpr int32_t kFixedStepQuanta = 1;
    static constexpr int32_t kMaxCatchUpSteps = 2;
    int32_t accumulatorQuanta_ = 0;
};
} // namespace Game::CarPhysicsV2

