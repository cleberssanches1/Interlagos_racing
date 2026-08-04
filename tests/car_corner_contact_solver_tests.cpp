#include <cstdint>
#include <iostream>

#include "car_corner_contact_solver.hpp"

namespace
{
int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { \
    std::cerr << __LINE__ << ": expected " #expr "\n"; ++failures; } } while (0)

void TestBootstrapRequiresFourCorners()
{
    Game::CarPhysics::CornerContactSolverState state{};
    const int32_t surface[4] = {0, 0, 0, 0};
    int32_t bodyY = 123;
    EXPECT_TRUE(!Game::CarPhysics::CornerContactSolver::Step(
        surface, 0x09u, -0x00004000, bodyY, state));
    EXPECT_TRUE(!state.initialized);
    EXPECT_TRUE(bodyY == 123);
    EXPECT_TRUE(Game::CarPhysics::CornerContactSolver::Step(
        surface, 0x0Fu, -0x00004000, bodyY, state));
    EXPECT_TRUE(state.initialized);
    EXPECT_TRUE(bodyY == -0x00004000);
}

void TestFlatContactDoesNotSinkOrTilt()
{
    Game::CarPhysics::CornerContactSolverState state{};
    const int32_t surface[4] = {0, 0, 0, 0};
    int32_t bodyY = 0;
    Game::CarPhysics::CornerContactSolver::Step(
        surface, 0x0Fu, -0x00004000, bodyY, state);
    for (int i = 0; i < 120; ++i)
    {
        Game::CarPhysics::CornerContactSolver::Step(
            surface, 0x0Fu, -0x00004000, bodyY, state);
    }
    EXPECT_TRUE(bodyY == -0x00004000);
    EXPECT_TRUE(state.pitchDeltaRaw == 0);
    EXPECT_TRUE(state.rollDeltaRaw == 0);
}

void TestOneDroppedCornerProducesPitchAndRoll()
{
    Game::CarPhysics::CornerContactSolverState state{};
    int32_t surface[4] = {0, 0, 0, 0};
    int32_t bodyY = 0;
    Game::CarPhysics::CornerContactSolver::Step(
        surface, 0x0Fu, -0x00004000, bodyY, state);
    surface[3] = 1 << 16; // rear-right asphalt drops first
    for (int i = 0; i < 8; ++i)
    {
        Game::CarPhysics::CornerContactSolver::Step(
            surface, 0x0Fu, -0x00004000, bodyY, state);
    }
    EXPECT_TRUE(state.pitchDeltaRaw < 0); // rear moves down relative to front
    EXPECT_TRUE(state.rollDeltaRaw > 0);  // right moves down relative to left
}

void TestAirborneBodyAcceleratesDownward()
{
    Game::CarPhysics::CornerContactSolverState state{};
    const int32_t surface[4] = {0, 0, 0, 0};
    int32_t bodyY = 0;
    Game::CarPhysics::CornerContactSolver::Step(
        surface, 0x0Fu, -0x00004000, bodyY, state);
    const int32_t initialY = bodyY;
    for (int i = 0; i < 4; ++i)
    {
        Game::CarPhysics::CornerContactSolver::Step(
            surface, 0x00u, -0x00004000, bodyY, state);
    }
    EXPECT_TRUE(bodyY > initialY);
    EXPECT_TRUE(state.verticalVelocityRaw > 0);
}

void TestLargeSurfaceDropDoesNotSnapBody()
{
    Game::CarPhysics::CornerContactSolverState state{};
    int32_t surface[4] = {0, 0, 0, 0};
    int32_t bodyY = 0;
    Game::CarPhysics::CornerContactSolver::Step(
        surface, 0x0Fu, -0x00004000, bodyY, state);
    const int32_t beforeDrop = bodyY;
    surface[0] = surface[1] = surface[2] = surface[3] = 10 << 16;
    Game::CarPhysics::CornerContactSolver::Step(
        surface, 0x0Fu, -0x00004000, bodyY, state);
    EXPECT_TRUE(bodyY - beforeDrop == Game::CarPhysics::CornerContactSolver::kGravityRaw);
    EXPECT_TRUE(bodyY < (1 << 16));
}

void TestLargeSurfaceRiseUsesLimitedCorrection()
{
    Game::CarPhysics::CornerContactSolverState state{};
    int32_t surface[4] = {0, 0, 0, 0};
    int32_t bodyY = 0;
    Game::CarPhysics::CornerContactSolver::Step(
        surface, 0x0Fu, -0x00004000, bodyY, state);
    const int32_t beforeRise = bodyY;
    surface[0] = surface[1] = surface[2] = surface[3] = -(10 << 16);
    Game::CarPhysics::CornerContactSolver::Step(
        surface, 0x0Fu, -0x00004000, bodyY, state);
    const int32_t correction = beforeRise - bodyY;
    EXPECT_TRUE(correction > 0);
    EXPECT_TRUE(correction <= Game::CarPhysics::CornerContactSolver::kMaxPositionCorrectionRaw);
}
} // namespace

int main()
{
    TestBootstrapRequiresFourCorners();
    TestFlatContactDoesNotSinkOrTilt();
    TestOneDroppedCornerProducesPitchAndRoll();
    TestAirborneBodyAcceleratesDownward();
    TestLargeSurfaceDropDoesNotSnapBody();
    TestLargeSurfaceRiseUsesLimitedCorrection();
    if (failures != 0)
    {
        std::cerr << "car_corner_contact_solver_tests: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "car_corner_contact_solver_tests: PASS\n";
    return 0;
}
