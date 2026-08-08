#pragma once

#include <cstdint>

namespace Game::CarPhysics
{
// Four-corner geometric plant (REDRIVER2-inspired, stable).
// Previous torque-integrator windup drove pitchDelta → max (embicada permanente
// + traseira alta, video 17-41-24) and gravity+lift fought heave (voo 17-44-44).
//
// Now: ease bodyY / pitchDelta / rollDelta toward contact targets each frame.
//   pitchDelta target = frontY − rearY   (Y-down: + ⇒ nose lower)
//   rollDelta  target = rightY − leftY
//   bodyY      target = avg(4) + rideOffset
struct CornerContactSolverState
{
    int32_t verticalVelocityRaw = 0; // retained for telemetry / optional blend
    int32_t pitchDeltaRaw = 0;
    int32_t rollDeltaRaw = 0;
    int32_t pitchVelocityRaw = 0;
    int32_t rollVelocityRaw = 0;
    bool initialized = false;
};

struct CornerContactSolverOutput
{
    uint8_t touchingMask = 0u;
    int32_t maxPenetrationRaw = 0;
    int32_t targetPitchDeltaRaw = 0;
    int32_t targetRollDeltaRaw = 0;
    int32_t targetBodyYRaw = 0;
};

class CornerContactSolver
{
public:
    // Y grows downward.
    // Per-frame approach to geometric targets (no unbounded torque).
    static constexpr int32_t kMaxBodyStepRaw = 0x000A0000;      // 10.0
    static constexpr int32_t kMaxAttitudeStepRaw = 0x00050000;  // 5.0 chord / frame
    // Senna faces: tan~0.39 → chord ~29 u.
    static constexpr int32_t kMaxAttitudeDeltaRaw = 0x001E0000; // 30.0
    // Faster approach to live plane (ref: continuous alignment).
    static constexpr int32_t kBodyBlendShift = 1;               // >>1
    static constexpr int32_t kAttitudeBlendShift = 1;

    static void Reset(CornerContactSolverState& state)
    {
        state = CornerContactSolverState{};
    }

    // surfaceYRaw: FL, FR, RL, RR. validMask bit i = corner i.
    static bool Step(const int32_t surfaceYRaw[4],
                     uint8_t validMask,
                     int32_t rideHeightOffsetRaw,
                     int32_t& bodyYRaw,
                     CornerContactSolverState& state,
                     CornerContactSolverOutput* output = nullptr)
    {
        CornerContactSolverOutput localOutput{};

        int32_t sum = 0;
        int32_t count = 0;
        int32_t frontSum = 0, frontN = 0;
        int32_t rearSum = 0, rearN = 0;
        int32_t leftSum = 0, leftN = 0;
        int32_t rightSum = 0, rightN = 0;

        for (uint8_t i = 0u; i < 4u; ++i)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << i);
            if ((validMask & bit) == 0u) continue;
            const int32_t y = surfaceYRaw[i];
            sum += y;
            ++count;
            localOutput.touchingMask |= bit;
            if (i < 2u)
            {
                frontSum += y;
                ++frontN;
            }
            else
            {
                rearSum += y;
                ++rearN;
            }
            if ((i == 0u) || (i == 2u))
            {
                leftSum += y;
                ++leftN;
            }
            else
            {
                rightSum += y;
                ++rightN;
            }
        }

        if (count == 0)
        {
            if (output) *output = localOutput;
            return false;
        }

        const int32_t avg = sum / count;
        const int32_t targetBody = avg + rideHeightOffsetRaw;
        localOutput.targetBodyYRaw = targetBody;

        int32_t targetPitch = 0;
        if (frontN > 0 && rearN > 0)
        {
            targetPitch = (frontSum / frontN) - (rearSum / rearN);
        }
        int32_t targetRoll = 0;
        if (leftN > 0 && rightN > 0)
        {
            targetRoll = (rightSum / rightN) - (leftSum / leftN);
        }
        targetPitch = Clamp(targetPitch, -kMaxAttitudeDeltaRaw, kMaxAttitudeDeltaRaw);
        targetRoll = Clamp(targetRoll, -kMaxAttitudeDeltaRaw, kMaxAttitudeDeltaRaw);
        localOutput.targetPitchDeltaRaw = targetPitch;
        localOutput.targetRollDeltaRaw = targetRoll;

        if (!state.initialized)
        {
            // Need both axles for a meaningful pitch sample.
            if (frontN == 0 || rearN == 0)
            {
                if (output) *output = localOutput;
                return false;
            }
            bodyYRaw = targetBody;
            state.pitchDeltaRaw = targetPitch;
            state.rollDeltaRaw = targetRoll;
            state.verticalVelocityRaw = 0;
            state.pitchVelocityRaw = 0;
            state.rollVelocityRaw = 0;
            state.initialized = true;
            if (output) *output = localOutput;
            return true;
        }

        // --- Body heave: ease toward contact average (no gravity integrator) ---
        {
            int32_t err = targetBody - bodyYRaw;
            // Penetration diagnostic (body deeper than plane, Y-down).
            if (err < 0)
            {
                localOutput.maxPenetrationRaw = -err;
            }
            int32_t step = err >> kBodyBlendShift;
            if (step == 0 && err != 0)
            {
                step = (err > 0) ? 1 : -1;
            }
            if (step > kMaxBodyStepRaw) step = kMaxBodyStepRaw;
            if (step < -kMaxBodyStepRaw) step = -kMaxBodyStepRaw;
            bodyYRaw += step;
            state.verticalVelocityRaw = step;
        }

        // --- Pitch / roll: ease toward geometric F−R / R−L targets ------------
        {
            int32_t errP = targetPitch - state.pitchDeltaRaw;
            int32_t stepP = errP >> kAttitudeBlendShift;
            if (stepP == 0 && errP != 0)
            {
                stepP = (errP > 0) ? 1 : -1;
            }
            if (stepP > kMaxAttitudeStepRaw) stepP = kMaxAttitudeStepRaw;
            if (stepP < -kMaxAttitudeStepRaw) stepP = -kMaxAttitudeStepRaw;
            state.pitchDeltaRaw += stepP;
            state.pitchVelocityRaw = stepP;

            int32_t errR = targetRoll - state.rollDeltaRaw;
            int32_t stepR = errR >> kAttitudeBlendShift;
            if (stepR == 0 && errR != 0)
            {
                stepR = (errR > 0) ? 1 : -1;
            }
            if (stepR > kMaxAttitudeStepRaw) stepR = kMaxAttitudeStepRaw;
            if (stepR < -kMaxAttitudeStepRaw) stepR = -kMaxAttitudeStepRaw;
            state.rollDeltaRaw += stepR;
            state.rollVelocityRaw = stepR;
        }

        state.pitchDeltaRaw = Clamp(state.pitchDeltaRaw,
                                    -kMaxAttitudeDeltaRaw,
                                    kMaxAttitudeDeltaRaw);
        state.rollDeltaRaw = Clamp(state.rollDeltaRaw,
                                   -kMaxAttitudeDeltaRaw,
                                   kMaxAttitudeDeltaRaw);

        if (output) *output = localOutput;
        return true;
    }

private:
    static int32_t Clamp(int32_t value, int32_t minimum, int32_t maximum)
    {
        if (value < minimum) return minimum;
        if (value > maximum) return maximum;
        return value;
    }
};
} // namespace Game::CarPhysics
