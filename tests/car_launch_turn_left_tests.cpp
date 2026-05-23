#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "car_dynamics_model.hpp"

namespace
{
using Game::CarPhysics::DynamicsModel;
using Game::CarPhysics::DynamicsState;
using Game::CarPhysics::FrameStepOutput;
using Game::CarPhysics::Fxp;
using Game::GameplayFrameState;
using Game::Vector3D;

struct TestContext
{
    int failures = 0;

    void Fail(const char* expr,
              const char* file,
              int line,
              const std::string& detail = std::string())
    {
        std::cerr << file << ":" << line << " failure: " << expr;
        if (!detail.empty()) std::cerr << " [" << detail << "]";
        std::cerr << "\n";
        ++failures;
    }
};

template <typename T>
std::string ToString(const T& value)
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

#define EXPECT_TRUE(ctx, expr) \
    do \
    { \
        if (!(expr)) (ctx).Fail(#expr, __FILE__, __LINE__); \
    } while (0)

#define EXPECT_NEAR_FXP(ctx, actual, expected, toleranceRaw) \
    do \
    { \
        const auto _a = (actual); \
        const auto _e = (expected); \
        const int32_t _d = _a.RawValue() - _e.RawValue(); \
        const int32_t _ad = (_d < 0) ? -_d : _d; \
        if (_ad > (toleranceRaw)) \
        { \
            (ctx).Fail(#actual " ~= " #expected, __FILE__, __LINE__, \
                       std::string("absDiffRaw=") + ToString(_ad)); \
        } \
    } while (0)

int32_t SignedYawDelta(int32_t beforeDeg, int32_t afterDeg)
{
    int32_t delta = (afterDeg - beforeDeg) % 360;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
    return delta;
}

void ComputeLocalDelta(const Vector3D& deltaWorld,
                       int32_t yawBeforeDeg,
                       Fxp& outLongitudinal,
                       Fxp& outLateral)
{
    const auto yawAngle = SRL::Math::Types::Angle::FromDegrees(
        Fxp::BuildRaw(static_cast<int32_t>(yawBeforeDeg) << 16));
    const Fxp sinYaw = SRL::Math::Trigonometry::Sin(yawAngle);
    const Fxp cosYaw = SRL::Math::Trigonometry::Cos(yawAngle);
    const Fxp negCosYaw = Fxp::BuildRaw(-cosYaw.RawValue());

    // Body axes:
    // forward = (sin(yaw), -cos(yaw))
    // right   = (cos(yaw),  sin(yaw))
    outLongitudinal = (sinYaw * deltaWorld.X) + (negCosYaw * deltaWorld.Z);
    outLateral = (cosYaw * deltaWorld.X) + (sinYaw * deltaWorld.Z);
}

void TestLaunchLeftTurnUsesArcWithoutInitialSideSlip(TestContext& ctx)
{
    const std::vector<int16_t> steeringInputs{-20, -40, -60, -80, -100};
    for (size_t i = 0; i < steeringInputs.size(); ++i)
    {
        GameplayFrameState frame{};
        frame.throttle = 100;
        frame.braking = false;
        frame.steering = steeringInputs[i];

        DynamicsState state{};
        DynamicsModel::Reset(state);

        Vector3D pos(0.0, 0.0, 0.0);
        int32_t yawDeg = 0;

        // Simulate a short launch window from standstill.
        for (int32_t step = 0; step < 16; ++step)
        {
            const Vector3D posBefore = pos;
            const int32_t yawBefore = yawDeg;
            FrameStepOutput out{};
            DynamicsModel::IntegratePlanar(frame, state, pos, yawDeg, out);
            const Vector3D delta = pos - posBefore;

            Fxp localLong{};
            Fxp localLat{};
            ComputeLocalDelta(delta, yawBefore, localLong, localLat);

            // The launch must move forward in car-local axis.
            EXPECT_TRUE(ctx, localLong.RawValue() > 0);

            // No side-slip burst: lateral local component must stay tiny.
            // 0.08 world units is a strict bound for first-order launch behavior.
            EXPECT_TRUE(ctx, localLat.Abs().RawValue() <= Fxp::BuildRaw(0x0000147B).RawValue());

            // Left steering must rotate yaw to left over time (negative signed delta).
            if (step > 1)
            {
                const int32_t yawDelta = SignedYawDelta(yawBefore, yawDeg);
                EXPECT_TRUE(ctx, yawDelta <= 0);
            }
        }

        // After launch, path must drift to world-left (negative X) for left steer.
        EXPECT_TRUE(ctx, pos.X.RawValue() < 0);
    }
}

void TestLaunchRightTurnUsesArcWithoutInitialSideSlip(TestContext& ctx)
{
    const std::vector<int16_t> steeringInputs{20, 40, 60, 80, 100};
    for (size_t i = 0; i < steeringInputs.size(); ++i)
    {
        GameplayFrameState frame{};
        frame.throttle = 100;
        frame.braking = false;
        frame.steering = steeringInputs[i];

        DynamicsState state{};
        DynamicsModel::Reset(state);

        Vector3D pos(0.0, 0.0, 0.0);
        int32_t yawDeg = 0;

        for (int32_t step = 0; step < 16; ++step)
        {
            const Vector3D posBefore = pos;
            const int32_t yawBefore = yawDeg;
            FrameStepOutput out{};
            DynamicsModel::IntegratePlanar(frame, state, pos, yawDeg, out);
            const Vector3D delta = pos - posBefore;

            Fxp localLong{};
            Fxp localLat{};
            ComputeLocalDelta(delta, yawBefore, localLong, localLat);

            EXPECT_TRUE(ctx, localLong.RawValue() > 0);
            EXPECT_TRUE(ctx, localLat.Abs().RawValue() <= Fxp::BuildRaw(0x0000147B).RawValue());

            if (step > 1)
            {
                const int32_t yawDelta = SignedYawDelta(yawBefore, yawDeg);
                EXPECT_TRUE(ctx, yawDelta >= 0);
            }
        }

        // Right steering should drift to world-right (positive X).
        EXPECT_TRUE(ctx, pos.X.RawValue() > 0);
    }
}

struct TestCase
{
    const char* name = "";
    void (*fn)(TestContext&) = nullptr;
};
} // namespace

int main()
{
    const std::vector<TestCase> tests{
        {"LaunchLeftTurnUsesArcWithoutInitialSideSlip", &TestLaunchLeftTurnUsesArcWithoutInitialSideSlip},
        {"LaunchRightTurnUsesArcWithoutInitialSideSlip", &TestLaunchRightTurnUsesArcWithoutInitialSideSlip},
    };

    TestContext ctx{};
    size_t passed = 0u;
    for (size_t i = 0; i < tests.size(); ++i)
    {
        try
        {
            tests[i].fn(ctx);
            if (ctx.failures == 0)
            {
                ++passed;
                std::cout << "[PASS] " << tests[i].name << "\n";
            }
            else
            {
                std::cout << "[FAIL] " << tests[i].name << "\n";
                return 1;
            }
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[EXCEPTION] " << tests[i].name << ": " << ex.what() << "\n";
            return 1;
        }
        catch (...)
        {
            std::cerr << "[EXCEPTION] " << tests[i].name << ": unknown\n";
            return 1;
        }
    }

    std::cout << "Passed " << passed << " test(s)\n";
    return 0;
}
