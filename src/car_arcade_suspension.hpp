#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Game::CarPhysics
{
struct ArcadeSuspensionState
{
    std::array<int32_t, 4> targetYRaw{{0, 0, 0, 0}};
    std::array<int32_t, 4> filteredYRaw{{0, 0, 0, 0}};
    std::array<int32_t, 4> velocityYRaw{{0, 0, 0, 0}};
    // First diagonal is staged so a new four-contact plane is committed only
    // after the complementary diagonal arrives on the following frame.
    std::array<int32_t, 4> stagedTargetYRaw{{0, 0, 0, 0}};
    std::array<int16_t, 4> segmentIds{{-1, -1, -1, -1}};
    std::array<int16_t, 4> faceIndices{{-1, -1, -1, -1}};
    std::array<uint8_t, 4> sampleAge{{0, 0, 0, 0}};
    std::array<uint8_t, 4> faceSwitchRejectCount{{0, 0, 0, 0}};
    uint8_t validMask = 0u;
    uint8_t stagedTargetMask = 0u;
    uint8_t diagonalPhase = 0u;
};
static_assert(sizeof(ArcadeSuspensionState) <= 96u,
              "Arcade suspension state must stay inside its 96-byte HWR budget.");

// Fixed-point, allocation-free suspension filter for the SH2 hot path.
// Wheel order: FL, FR, RL, RR. Y grows downward in this project.
class ArcadeSuspensionFilter
{
public:
    // A diagonal is refreshed every two frames. These caps still follow the
    // steep S do Senna grade while spreading a large face seam across frames.
    // The audited S do Senna mesh reaches tan~=0.39. Even near 150-200 km/h a
    // wheel can legitimately move more than 16 Y units between its alternating
    // diagonal samples. Keep the cap below the known ~40-unit wrong-deck jump.
    static constexpr int32_t kMaxObservedStepRaw = 0x00200000;     // 32 climb
    static constexpr int32_t kMaxObservedStepDownRaw = 0x00400000; // 64 descent / junction
    static constexpr int32_t kMaxWheelSpeedRaw = 0x00140000;       // 20 units/frame
    static constexpr int32_t kSettleThresholdRaw = 0x00000800; // 0.03125
    // Climb face flips still need confirmation. Descent/junction never stalls.
    static constexpr int32_t kMaxFaceSwitchDeltaRaw = 0x00200000; // 32 units
    static constexpr uint8_t kSampleHoldFrames = 8u;
    static constexpr uint8_t kFaceSwitchConfirmSamples = 3u;

    static void Reset(ArcadeSuspensionState& state)
    {
        state = ArcadeSuspensionState{};
    }

    static void BeginFrame(ArcadeSuspensionState& state)
    {
        for (uint8_t i = 0u; i < 4u; ++i)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << i);
            if ((state.validMask & bit) == 0u) continue;
            if (state.sampleAge[i] < 0xFFu) ++state.sampleAge[i];
            if (state.sampleAge[i] > kSampleHoldFrames)
            {
                state.validMask = static_cast<uint8_t>(state.validMask & ~bit);
                state.velocityYRaw[i] = 0;
                state.segmentIds[i] = -1;
                state.faceIndices[i] = -1;
                state.faceSwitchRejectCount[i] = 0u;
            }
        }
    }

    static bool Observe(ArcadeSuspensionState& state,
                        uint8_t wheelIndex,
                        int32_t surfaceYRaw,
                        int32_t segmentId,
                        int32_t faceIndex = -1)
    {
        if (wheelIndex >= 4u) return false;
        const uint8_t bit = static_cast<uint8_t>(1u << wheelIndex);
        const bool previousFaceKnown =
            (state.validMask & bit) != 0u &&
            state.segmentIds[wheelIndex] > 0 &&
            state.faceIndices[wheelIndex] >= 0;
        const bool newFaceKnown = segmentId > 0 && faceIndex >= 0;
        const bool faceChanged = previousFaceKnown && newFaceKnown &&
            (state.segmentIds[wheelIndex] != segmentId ||
             state.faceIndices[wheelIndex] != faceIndex);
        // Y-down: larger Y = lower altitude = descending into next segment face.
        const bool descending = (state.validMask & bit) != 0u &&
            surfaceYRaw > state.targetYRaw[wheelIndex];
        // Reject only large climb/noise flips. Descent must accept junctions.
        if (faceChanged && !descending &&
            Abs(surfaceYRaw - state.targetYRaw[wheelIndex]) > kMaxFaceSwitchDeltaRaw &&
            state.faceSwitchRejectCount[wheelIndex] + 1u < kFaceSwitchConfirmSamples)
        {
            ++state.faceSwitchRejectCount[wheelIndex];
            return false;
        }
        state.faceSwitchRejectCount[wheelIndex] = 0u;
        if ((state.validMask & bit) == 0u)
        {
            state.targetYRaw[wheelIndex] = surfaceYRaw;
            state.filteredYRaw[wheelIndex] = surfaceYRaw;
            state.velocityYRaw[wheelIndex] = 0;
            state.validMask = static_cast<uint8_t>(state.validMask | bit);
        }
        else
        {
            const int32_t previous = state.targetYRaw[wheelIndex];
            const int32_t delta = Clamp(surfaceYRaw - previous,
                                        -kMaxObservedStepRaw,
                                        kMaxObservedStepDownRaw);
            state.targetYRaw[wheelIndex] = previous + delta;
        }
        state.segmentIds[wheelIndex] = static_cast<int16_t>(
            Clamp(segmentId, -1, 32767));
        state.faceIndices[wheelIndex] = static_cast<int16_t>(
            Clamp(faceIndex, -1, 32767));
        state.sampleAge[wheelIndex] = 0u;
        return true;
    }

    static void StepWheels(ArcadeSuspensionState& state)
    {
        for (uint8_t i = 0u; i < 4u; ++i)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << i);
            if ((state.validMask & bit) == 0u) continue;

            const int32_t error = state.targetYRaw[i] - state.filteredYRaw[i];
            // spring=1/4 error; damping retains 1/2 velocity.
            int32_t velocity = (state.velocityYRaw[i] >> 1) + (error >> 2);
            velocity = Clamp(velocity, -kMaxWheelSpeedRaw, kMaxWheelSpeedRaw);
            int32_t next = state.filteredYRaw[i] + velocity;
            if ((error > 0 && next > state.targetYRaw[i]) ||
                (error < 0 && next < state.targetYRaw[i]))
            {
                next = state.targetYRaw[i];
                velocity = 0;
            }
            if (Abs(state.targetYRaw[i] - next) <= kSettleThresholdRaw &&
                Abs(velocity) <= kSettleThresholdRaw)
            {
                next = state.targetYRaw[i];
                velocity = 0;
            }
            state.filteredYRaw[i] = next;
            state.velocityYRaw[i] = velocity;
        }
    }

    // Observe() performs all validation immediately, but the low-cost runtime
    // samples only one diagonal per frame. Defer the accepted target from the
    // first diagonal and publish it together with the second diagonal. Face
    // hints remain current while pitch/roll/heave see one coherent plane.
    static void DeferObservedTarget(ArcadeSuspensionState& state,
                                    uint8_t wheelIndex,
                                    int32_t previousTargetYRaw)
    {
        if (wheelIndex >= 4u) return;
        state.stagedTargetYRaw[wheelIndex] = state.targetYRaw[wheelIndex];
        state.stagedTargetMask = static_cast<uint8_t>(
            state.stagedTargetMask | static_cast<uint8_t>(1u << wheelIndex));
        state.targetYRaw[wheelIndex] = previousTargetYRaw;
    }

    static void CommitDeferredTargets(ArcadeSuspensionState& state)
    {
        for (uint8_t i = 0u; i < 4u; ++i)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << i);
            if ((state.stagedTargetMask & bit) == 0u) continue;
            state.targetYRaw[i] = state.stagedTargetYRaw[i];
        }
        state.stagedTargetMask = 0u;
    }

    static bool Read(const ArcadeSuspensionState& state,
                     uint8_t wheelIndex,
                     int32_t& outYRaw,
                     int32_t& outSegmentId,
                     int32_t* outFaceIndex = nullptr)
    {
        if (wheelIndex >= 4u) return false;
        const uint8_t bit = static_cast<uint8_t>(1u << wheelIndex);
        if ((state.validMask & bit) == 0u) return false;
        outYRaw = state.filteredYRaw[wheelIndex];
        outSegmentId = state.segmentIds[wheelIndex];
        if (outFaceIndex) *outFaceIndex = state.faceIndices[wheelIndex];
        return true;
    }

    // Common-mode road height must not inherit the spring lag used for wheel
    // attitude. Targets are already strict-XZ, face-confirmed and step-capped.
    // Alternating diagonals update two opposite corners per frame, so their
    // four-corner mean supplies a cheap 60 Hz chassis plane.
    static bool AverageTargetYRaw(const ArcadeSuspensionState& state,
                                  int32_t& outYRaw)
    {
        int64_t sum = 0;
        int32_t count = 0;
        for (uint8_t i = 0u; i < 4u; ++i)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << i);
            if ((state.validMask & bit) == 0u) continue;
            sum += static_cast<int64_t>(state.targetYRaw[i]);
            ++count;
        }
        if (count == 0) return false;
        outYRaw = static_cast<int32_t>(sum / count);
        return true;
    }

    // Decompose four contacts [FL, FR, RL, RR] into a rigid center/pitch/roll
    // plane plus one residual per wheel. A perfect plane yields zero residual;
    // only bumps and torsional irregularities remain for visual wheel travel.
    static void FitContactPlaneResiduals(
        const std::array<int32_t, 4>& contactYRaw,
        std::array<int32_t, 4>& outResidualYRaw)
    {
        const int32_t centerRaw = static_cast<int32_t>(
            (static_cast<int64_t>(contactYRaw[0]) + contactYRaw[1] +
             contactYRaw[2] + contactYRaw[3]) / 4);
        const int32_t frontRaw = static_cast<int32_t>(
            (static_cast<int64_t>(contactYRaw[0]) + contactYRaw[1]) / 2);
        const int32_t rearRaw = static_cast<int32_t>(
            (static_cast<int64_t>(contactYRaw[2]) + contactYRaw[3]) / 2);
        const int32_t leftRaw = static_cast<int32_t>(
            (static_cast<int64_t>(contactYRaw[0]) + contactYRaw[2]) / 2);
        const int32_t rightRaw = static_cast<int32_t>(
            (static_cast<int64_t>(contactYRaw[1]) + contactYRaw[3]) / 2);
        const int32_t pitchHalfRaw = (frontRaw - rearRaw) >> 1;
        const int32_t rollHalfRaw = (rightRaw - leftRaw) >> 1;
        const std::array<int32_t, 4> predictedRaw{{
            centerRaw + pitchHalfRaw - rollHalfRaw,
            centerRaw + pitchHalfRaw + rollHalfRaw,
            centerRaw - pitchHalfRaw - rollHalfRaw,
            centerRaw - pitchHalfRaw + rollHalfRaw
        }};
        for (uint8_t i = 0u; i < 4u; ++i)
        {
            outResidualYRaw[i] = contactYRaw[i] - predictedRaw[i];
        }
    }

    // Velocity-aware sprung body. Road motion supplies the equilibrium
    // velocity, while the chassis approaches that velocity through a damped
    // response. Unlike direct feed-forward, a face step cannot teleport the
    // chassis; unlike a plain spring, a constant ramp has no permanent lag.
    static int32_t StepChassisTracking(int32_t targetYRaw,
                                       int32_t currentYRaw,
                                       int32_t previousTargetYRaw,
                                       int32_t& velocityYRaw,
                                       int32_t maxUpSpeedRaw,
                                       int32_t maxDownSpeedRaw)
    {
        const int32_t targetMotion = Clamp(targetYRaw - previousTargetYRaw,
                                           -maxUpSpeedRaw,
                                           maxDownSpeedRaw);
        const int32_t error = targetYRaw - currentYRaw;
        const int32_t desiredVelocity = Clamp(
            targetMotion + (error >> 3),
            -maxUpSpeedRaw,
            maxDownSpeedRaw);
        // Quarter-step toward the desired velocity is a cheap critically
        // damped response for the 30/60 Hz Saturn update range.
        velocityYRaw += (desiredVelocity - velocityYRaw) >> 2;
        velocityYRaw = Clamp(velocityYRaw,
                             -maxUpSpeedRaw,
                             maxDownSpeedRaw);
        int32_t next = currentYRaw + velocityYRaw;

        if ((targetYRaw >= currentYRaw && next > targetYRaw) ||
            (targetYRaw <= currentYRaw && next < targetYRaw))
        {
            next = targetYRaw;
            velocityYRaw = targetMotion;
        }
        if (Abs(targetYRaw - next) <= kSettleThresholdRaw &&
            Abs(velocityYRaw - targetMotion) <= kSettleThresholdRaw)
        {
            next = targetYRaw;
            velocityYRaw = targetMotion;
        }
        return next;
    }

    static int32_t StepChassis(int32_t targetYRaw,
                               int32_t currentYRaw,
                               int32_t& velocityYRaw,
                               int32_t maxUpSpeedRaw,
                               int32_t maxDownSpeedRaw)
    {
        const int32_t error = targetYRaw - currentYRaw;
        // spring=3/16 error; damping retains 5/8 velocity.
        const int32_t spring = (error >> 3) + (error >> 4);
        const int32_t dampedVelocity =
            (velocityYRaw >> 1) + (velocityYRaw >> 3);
        velocityYRaw = Clamp(dampedVelocity + spring,
                             -maxUpSpeedRaw,
                             maxDownSpeedRaw);
        int32_t next = currentYRaw + velocityYRaw;
        if ((error > 0 && next > targetYRaw) ||
            (error < 0 && next < targetYRaw))
        {
            next = targetYRaw;
            velocityYRaw = 0;
        }
        if (Abs(targetYRaw - next) <= kSettleThresholdRaw &&
            Abs(velocityYRaw) <= kSettleThresholdRaw)
        {
            next = targetYRaw;
            velocityYRaw = 0;
        }
        return next;
    }

private:
    static int32_t Clamp(int32_t value, int32_t minimum, int32_t maximum)
    {
        if (value < minimum) return minimum;
        if (value > maximum) return maximum;
        return value;
    }

    static int32_t Abs(int32_t value)
    {
        return (value < 0) ? -value : value;
    }
};
} // namespace Game::CarPhysics
