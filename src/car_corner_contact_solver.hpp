#pragma once

#include <cstdint>

namespace Game::CarPhysics
{
// Low-cost four-corner contact state. Attitude is stored as the vertical
// wheel-to-wheel delta, avoiding trigonometry in the physics path:
//   pitchDelta = frontY - rearY; rollDelta = rightY - leftY.
struct CornerContactSolverState
{
    int32_t verticalVelocityRaw = 0;
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
};

class CornerContactSolver
{
public:
    // Y grows downward in this project.
    static constexpr int32_t kGravityRaw = 0x00000800;          // 0.03125/frame^2
    static constexpr int32_t kMaxFallSpeedRaw = 0x0000C000;     // 0.75/frame
    static constexpr int32_t kMaxPositionCorrectionRaw = 0x00006000; // 0.375/frame
    static constexpr int32_t kMaxAttitudeDeltaRaw = 0x00020000; // 2.0 across axle/track
    static constexpr int32_t kMaxAngularVelocityRaw = 0x00002000;

    static void Reset(CornerContactSolverState& state)
    {
        state = CornerContactSolverState{};
    }

    static bool Step(const int32_t surfaceYRaw[4],
                     uint8_t validMask,
                     int32_t rideHeightOffsetRaw,
                     int32_t& bodyYRaw,
                     CornerContactSolverState& state,
                     CornerContactSolverOutput* output = nullptr)
    {
        CornerContactSolverOutput localOutput{};

        // Bootstrap only after all four corners have been observed. This occurs
        // after two diagonal phases and prevents a spawn-time attitude impulse.
        if (!state.initialized)
        {
            if ((validMask & 0x0Fu) != 0x0Fu)
            {
                if (output) *output = localOutput;
                return false;
            }

            const int32_t front = Average2(surfaceYRaw[0], surfaceYRaw[1]);
            const int32_t rear = Average2(surfaceYRaw[2], surfaceYRaw[3]);
            const int32_t left = Average2(surfaceYRaw[0], surfaceYRaw[2]);
            const int32_t right = Average2(surfaceYRaw[1], surfaceYRaw[3]);
            bodyYRaw = Average4(surfaceYRaw) + rideHeightOffsetRaw;
            state.pitchDeltaRaw = Clamp(front - rear,
                                        -kMaxAttitudeDeltaRaw,
                                        kMaxAttitudeDeltaRaw);
            state.rollDeltaRaw = Clamp(right - left,
                                       -kMaxAttitudeDeltaRaw,
                                       kMaxAttitudeDeltaRaw);
            state.verticalVelocityRaw = 0;
            state.pitchVelocityRaw = 0;
            state.rollVelocityRaw = 0;
            state.initialized = true;
            if (output) *output = localOutput;
            return true;
        }

        state.verticalVelocityRaw = Clamp(
            state.verticalVelocityRaw + kGravityRaw,
            -kMaxFallSpeedRaw,
            kMaxFallSpeedRaw);
        bodyYRaw += state.verticalVelocityRaw;

        state.pitchDeltaRaw += state.pitchVelocityRaw;
        state.rollDeltaRaw += state.rollVelocityRaw;
        state.pitchVelocityRaw -= state.pitchVelocityRaw >> 2;
        state.rollVelocityRaw -= state.rollVelocityRaw >> 2;

        int32_t frontPenetration = 0;
        int32_t rearPenetration = 0;
        int32_t leftPenetration = 0;
        int32_t rightPenetration = 0;

        for (uint8_t i = 0u; i < 4u; ++i)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << i);
            if ((validMask & bit) == 0u) continue;

            const bool front = i < 2u;
            const bool right = (i == 1u) || (i == 3u);
            const int32_t pitchOffset = front
                ? (state.pitchDeltaRaw >> 1)
                : -(state.pitchDeltaRaw >> 1);
            const int32_t rollOffset = right
                ? (state.rollDeltaRaw >> 1)
                : -(state.rollDeltaRaw >> 1);
            const int32_t supportY = bodyYRaw - rideHeightOffsetRaw +
                                     pitchOffset + rollOffset;
            const int32_t penetration = supportY - surfaceYRaw[i];
            if (penetration <= 0) continue;

            localOutput.touchingMask |= bit;
            if (penetration > localOutput.maxPenetrationRaw)
            {
                localOutput.maxPenetrationRaw = penetration;
            }
            if (front) frontPenetration += penetration;
            else rearPenetration += penetration;
            if (right) rightPenetration += penetration;
            else leftPenetration += penetration;
        }

        if (localOutput.touchingMask != 0u)
        {
            // Position-based normal constraint. Limiting the correction avoids
            // reproducing a mesh height discontinuity as a one-frame snap.
            bodyYRaw -= Clamp(localOutput.maxPenetrationRaw,
                              0,
                              kMaxPositionCorrectionRaw);

            // Arcade suspension: contact cancels downward speed without bounce.
            // Gravity is integrated again next frame, keeping tires pressed down.
            if (state.verticalVelocityRaw > 0)
            {
                state.verticalVelocityRaw = 0;
            }

            // Unequal corner reactions rotate the body around the first supports.
            const int32_t pitchMoment = rearPenetration - frontPenetration;
            const int32_t rollMoment = leftPenetration - rightPenetration;
            state.pitchVelocityRaw = Clamp(
                state.pitchVelocityRaw + (pitchMoment >> 4),
                -kMaxAngularVelocityRaw,
                kMaxAngularVelocityRaw);
            state.rollVelocityRaw = Clamp(
                state.rollVelocityRaw + (rollMoment >> 4),
                -kMaxAngularVelocityRaw,
                kMaxAngularVelocityRaw);
            state.pitchDeltaRaw += pitchMoment >> 2;
            state.rollDeltaRaw += rollMoment >> 2;
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

    static int32_t Average2(int32_t a, int32_t b)
    {
        return static_cast<int32_t>((static_cast<int64_t>(a) + b) >> 1);
    }

    static int32_t Average4(const int32_t values[4])
    {
        const int64_t sum = static_cast<int64_t>(values[0]) + values[1] +
                            values[2] + values[3];
        return static_cast<int32_t>(sum >> 2);
    }
};
} // namespace Game::CarPhysics
