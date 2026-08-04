#include <cstdint>
#include <iostream>

#include "car_arcade_suspension.hpp"
#include "car_contact_geometry.hpp"

namespace
{
int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { \
    std::cerr << __LINE__ << ": expected " #expr "\n"; ++failures; } } while (0)

void ObserveFlat(Game::CarPhysics::ArcadeSuspensionState& state)
{
    for (uint8_t i = 0u; i < 4u; ++i)
    {
        Game::CarPhysics::ArcadeSuspensionFilter::Observe(state, i, 0, 22);
    }
}

void TestFlatSurfaceStaysStable()
{
    Game::CarPhysics::ArcadeSuspensionState state{};
    ObserveFlat(state);
    for (int frame = 0; frame < 120; ++frame)
    {
        Game::CarPhysics::ArcadeSuspensionFilter::StepWheels(state);
    }
    EXPECT_TRUE(state.validMask == 0x0Fu);
    for (uint8_t i = 0u; i < 4u; ++i)
    {
        EXPECT_TRUE(state.filteredYRaw[i] == 0);
        EXPECT_TRUE(state.velocityYRaw[i] == 0);
    }
}

void TestFaceStepIsRateLimitedAndDamped()
{
    Game::CarPhysics::ArcadeSuspensionState state{};
    ObserveFlat(state);
    const int32_t fortyUnits = 40 << 16;
    Game::CarPhysics::ArcadeSuspensionFilter::Observe(state, 0u, fortyUnits, 23);
    EXPECT_TRUE(state.targetYRaw[0] ==
                Game::CarPhysics::ArcadeSuspensionFilter::kMaxObservedStepRaw);
    Game::CarPhysics::ArcadeSuspensionFilter::StepWheels(state);
    EXPECT_TRUE(state.filteredYRaw[0] > 0);
    EXPECT_TRUE(state.filteredYRaw[0] < state.targetYRaw[0]);
    EXPECT_TRUE(state.filteredYRaw[1] == 0);
}

void TestCachedCornersSurviveDiagonalAlternation()
{
    Game::CarPhysics::ArcadeSuspensionState state{};
    ObserveFlat(state);
    for (uint8_t frame = 0u; frame < 4u; ++frame)
    {
        Game::CarPhysics::ArcadeSuspensionFilter::BeginFrame(state);
    }
    EXPECT_TRUE(state.validMask == 0x0Fu);
    for (uint8_t frame = 0u; frame < 3u; ++frame)
    {
        Game::CarPhysics::ArcadeSuspensionFilter::BeginFrame(state);
    }
    EXPECT_TRUE(state.validMask == 0u);
}

void TestChassisDoesNotSnapOnLargeDrop()
{
    int32_t velocity = 0;
    const int32_t target = 10 << 16;
    const int32_t next = Game::CarPhysics::ArcadeSuspensionFilter::StepChassis(
        target, 0, velocity, 2 << 16, 8 << 16);
    EXPECT_TRUE(next > 0);
    EXPECT_TRUE(next < target);
    EXPECT_TRUE(velocity == next);
}

void TestChassisConvergesWithoutOvershoot()
{
    int32_t velocity = 0;
    const int32_t target = 3 << 16;
    int32_t current = 0;
    for (int frame = 0; frame < 120; ++frame)
    {
        const int32_t previous = current;
        current = Game::CarPhysics::ArcadeSuspensionFilter::StepChassis(
            target, current, velocity, 2 << 16, 8 << 16);
        EXPECT_TRUE(current >= previous);
        EXPECT_TRUE(current <= target);
    }
    EXPECT_TRUE(current == target);
    EXPECT_TRUE(velocity == 0);
}

void TestContinuousDescentHasBoundedWheelMotion()
{
    Game::CarPhysics::ArcadeSuspensionState state{};
    ObserveFlat(state);
    int32_t previous = 0;
    for (int frame = 0; frame < 40; ++frame)
    {
        if ((frame & 1) == 0)
        {
            const int32_t surface = (frame / 2 + 1) * (2 << 16);
            Game::CarPhysics::ArcadeSuspensionFilter::Observe(state, 0u, surface, 22);
        }
        Game::CarPhysics::ArcadeSuspensionFilter::StepWheels(state);
        const int32_t current = state.filteredYRaw[0];
        EXPECT_TRUE(current >= previous);
        EXPECT_TRUE(current - previous <=
                    Game::CarPhysics::ArcadeSuspensionFilter::kMaxWheelSpeedRaw);
        previous = current;
    }
}

void TestTargetPlaneDoesNotInheritWheelSpringLag()
{
    Game::CarPhysics::ArcadeSuspensionState state{};
    ObserveFlat(state);
    Game::CarPhysics::ArcadeSuspensionFilter::Observe(
        state, 0u, 40 << 16, 23);
    Game::CarPhysics::ArcadeSuspensionFilter::StepWheels(state);

    int32_t centerYRaw = 0;
    EXPECT_TRUE(Game::CarPhysics::ArcadeSuspensionFilter::AverageTargetYRaw(
        state, centerYRaw));
    EXPECT_TRUE(centerYRaw == (8 << 16));
    EXPECT_TRUE(state.filteredYRaw[0] < state.targetYRaw[0]);
}

void TestChassisTracksContinuousRampWithoutFloating()
{
    int32_t current = 0;
    int32_t previousTarget = 0;
    int32_t velocity = 0;
    for (int frame = 1; frame <= 40; ++frame)
    {
        const int32_t target = frame * (4 << 16);
        current = Game::CarPhysics::ArcadeSuspensionFilter::StepChassisTracking(
            target,
            current,
            previousTarget,
            velocity,
            16 << 16,
            16 << 16);
        EXPECT_TRUE(current <= target);
        EXPECT_TRUE(current >= 0);
        previousTarget = target;
    }
    // The velocity-aware spring is allowed a short compression transient, but
    // converges to the road velocity and removes steady ramp separation.
    EXPECT_TRUE(current == (40 * (4 << 16)));
    EXPECT_TRUE(velocity == (4 << 16));
}

void TestChassisTrackingStillCapsAOneFrameStep()
{
    int32_t current = 0;
    int32_t previousTarget = 0;
    int32_t velocity = 0;
    const int32_t target = 32 << 16;
    current = Game::CarPhysics::ArcadeSuspensionFilter::StepChassisTracking(
        target,
        current,
        previousTarget,
        velocity,
        16 << 16,
        16 << 16);
    EXPECT_TRUE(current > 0);
    EXPECT_TRUE(current < (16 << 16));
    EXPECT_TRUE(velocity == current);
    previousTarget = target;
    for (int frame = 0; frame < 120; ++frame)
    {
        const int32_t before = current;
        current = Game::CarPhysics::ArcadeSuspensionFilter::StepChassisTracking(
            target,
            current,
            previousTarget,
            velocity,
            16 << 16,
            16 << 16);
        EXPECT_TRUE(current >= before);
        EXPECT_TRUE(current <= target);
    }
    EXPECT_TRUE(current == target);
}

void TestContactFootprintMatchesCarModelScale()
{
    using namespace Game::CarPhysics::ContactGeometry;
    EXPECT_TRUE(kWheelbaseRaw == (75 << 16));
    EXPECT_TRUE(kTrackRaw > (44 << 16));
    EXPECT_TRUE(kTrackRaw < (45 << 16));

    const int32_t gradeRaw = (1 << 16) / 10; // 10%
    const int32_t deltaYRaw = static_cast<int32_t>(
        (static_cast<int64_t>(kWheelbaseRaw) * gradeRaw) >> 16);
    const int32_t reconstructedRaw = static_cast<int32_t>(
        (static_cast<int64_t>(deltaYRaw) << 16) / kWheelbaseRaw);
    const int32_t error = reconstructedRaw - gradeRaw;
    EXPECT_TRUE(error >= -1 && error <= 1);
}

void TestFirstDiagonalRemainsUnpublishedUntilCycleCommit()
{
    Game::CarPhysics::ArcadeSuspensionState state{};
    ObserveFlat(state);
    Game::CarPhysics::ArcadeSuspensionFilter::StepWheels(state);

    const int32_t drop = 8 << 16;
    int32_t previous = state.targetYRaw[0];
    Game::CarPhysics::ArcadeSuspensionFilter::Observe(state, 0u, drop, 22);
    Game::CarPhysics::ArcadeSuspensionFilter::DeferObservedTarget(
        state, 0u, previous);
    previous = state.targetYRaw[3];
    Game::CarPhysics::ArcadeSuspensionFilter::Observe(state, 3u, drop, 22);
    Game::CarPhysics::ArcadeSuspensionFilter::DeferObservedTarget(
        state, 3u, previous);
    Game::CarPhysics::ArcadeSuspensionFilter::StepWheels(state);
    // Phase A is staging only: published/filtered wheel heights stay coherent.
    EXPECT_TRUE(state.filteredYRaw[0] == 0);
    EXPECT_TRUE(state.filteredYRaw[1] == 0);
    EXPECT_TRUE(state.filteredYRaw[2] == 0);
    EXPECT_TRUE(state.filteredYRaw[3] == 0);

    Game::CarPhysics::ArcadeSuspensionFilter::Observe(state, 1u, drop, 22);
    Game::CarPhysics::ArcadeSuspensionFilter::Observe(state, 2u, drop, 22);
    Game::CarPhysics::ArcadeSuspensionFilter::CommitDeferredTargets(state);
    Game::CarPhysics::ArcadeSuspensionFilter::StepWheels(state);
    EXPECT_TRUE(state.filteredYRaw[0] == state.filteredYRaw[1]);
    EXPECT_TRUE(state.filteredYRaw[1] == state.filteredYRaw[2]);
    EXPECT_TRUE(state.filteredYRaw[2] == state.filteredYRaw[3]);
    EXPECT_TRUE(state.filteredYRaw[0] > 0);
}

void TestChassisRejectsAlternatingFaceNoise()
{
    int32_t current = 0;
    int32_t previousTarget = 0;
    int32_t velocity = 0;
    int32_t maximumAbs = 0;
    for (int frame = 0; frame < 60; ++frame)
    {
        const int32_t target = ((frame & 1) == 0) ? (1 << 16) : -(1 << 16);
        current = Game::CarPhysics::ArcadeSuspensionFilter::StepChassisTracking(
            target, current, previousTarget, velocity, 16 << 16, 16 << 16);
        const int32_t absCurrent = (current < 0) ? -current : current;
        if (absCurrent > maximumAbs) maximumAbs = absCurrent;
        previousTarget = target;
    }
    EXPECT_TRUE(maximumAbs < (1 << 16));
}

void TestContactPlaneLeavesOnlyBumpResidual()
{
    using Filter = Game::CarPhysics::ArcadeSuspensionFilter;
    std::array<int32_t, 4> planar{{
        10 << 16, 12 << 16, 30 << 16, 32 << 16
    }};
    std::array<int32_t, 4> residual{{0, 0, 0, 0}};
    Filter::FitContactPlaneResiduals(planar, residual);
    for (int32_t value : residual) EXPECT_TRUE(value == 0);

    planar[0] += 4 << 16;
    Filter::FitContactPlaneResiduals(planar, residual);
    EXPECT_TRUE(residual[0] == (1 << 16));
    EXPECT_TRUE(residual[1] == -(1 << 16));
    EXPECT_TRUE(residual[2] == -(1 << 16));
    EXPECT_TRUE(residual[3] == (1 << 16));
}

void TestWheelLocalFaceHintPersists()
{
    Game::CarPhysics::ArcadeSuspensionState state{};
    EXPECT_TRUE(Game::CarPhysics::ArcadeSuspensionFilter::Observe(
        state, 0u, 4 << 16, 22, 17));
    int32_t yRaw = 0;
    int32_t segmentId = -1;
    int32_t faceIndex = -1;
    EXPECT_TRUE(Game::CarPhysics::ArcadeSuspensionFilter::Read(
        state, 0u, yRaw, segmentId, &faceIndex));
    EXPECT_TRUE(segmentId == 22);
    EXPECT_TRUE(faceIndex == 17);
    EXPECT_TRUE(yRaw == (4 << 16));
}

void TestImplausibleFaceSwitchNeedsConfirmation()
{
    Game::CarPhysics::ArcadeSuspensionState state{};
    EXPECT_TRUE(Game::CarPhysics::ArcadeSuspensionFilter::Observe(
        state, 1u, 0, 30, 4));
    const int32_t wrongDeck = 40 << 16;
    EXPECT_TRUE(!Game::CarPhysics::ArcadeSuspensionFilter::Observe(
        state, 1u, wrongDeck, 30, 91));
    EXPECT_TRUE(!Game::CarPhysics::ArcadeSuspensionFilter::Observe(
        state, 1u, wrongDeck, 30, 91));
    EXPECT_TRUE(state.faceIndices[1] == 4);
    // A real landing/topology change eventually passes, but Observe still
    // clamps the target so it cannot become a one-frame body teleport.
    EXPECT_TRUE(Game::CarPhysics::ArcadeSuspensionFilter::Observe(
        state, 1u, wrongDeck, 30, 91));
    EXPECT_TRUE(state.faceIndices[1] == 91);
    EXPECT_TRUE(state.targetYRaw[1] ==
                Game::CarPhysics::ArcadeSuspensionFilter::kMaxObservedStepRaw);
}

} // namespace

int main()
{
    TestFlatSurfaceStaysStable();
    TestFaceStepIsRateLimitedAndDamped();
    TestCachedCornersSurviveDiagonalAlternation();
    TestChassisDoesNotSnapOnLargeDrop();
    TestChassisConvergesWithoutOvershoot();
    TestContinuousDescentHasBoundedWheelMotion();
    TestTargetPlaneDoesNotInheritWheelSpringLag();
    TestChassisTracksContinuousRampWithoutFloating();
    TestChassisTrackingStillCapsAOneFrameStep();
    TestContactFootprintMatchesCarModelScale();
    TestFirstDiagonalRemainsUnpublishedUntilCycleCommit();
    TestChassisRejectsAlternatingFaceNoise();
    TestContactPlaneLeavesOnlyBumpResidual();
    TestWheelLocalFaceHintPersists();
    TestImplausibleFaceSwitchNeedsConfirmation();
    if (failures != 0)
    {
        std::cerr << "car_arcade_suspension_tests: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "car_arcade_suspension_tests: PASS\n";
    return 0;
}
