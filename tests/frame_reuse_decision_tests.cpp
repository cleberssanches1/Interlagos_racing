#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

#include "frame_reuse_decision_model.hpp"

namespace
{
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

#define EXPECT_EQ(ctx, actual, expected) \
    do \
    { \
        const auto actualValue = (actual); \
        const auto expectedValue = (expected); \
        if (!(actualValue == expectedValue)) \
        { \
            (ctx).Fail(#actual " == " #expected, \
                       __FILE__, \
                       __LINE__, \
                       std::string("actual=") + ToString(actualValue) + \
                       " expected=" + ToString(expectedValue)); \
        } \
    } while (0)

void TestSimulationLockstepConsumesExactFrame(TestContext& ctx)
{
    FrameReuseModel::SimulationReuseInputs inputs{};
    inputs.requestFrameId = 42u;
    inputs.committedFrameId = 42u;
    inputs.writeIdx = 1u;
    inputs.committedIdx = 0u;
    inputs.slaveSimulationEnabled = true;
    inputs.lockstepEnabled = true;
    inputs.jobInFlight = false;
    inputs.hasCommittedPacket = true;

    const auto decision = FrameReuseModel::ComputeSimulationReuseDecision(inputs);
    EXPECT_TRUE(ctx, decision.valid);
    EXPECT_TRUE(ctx, decision.hasExactFrameCandidate);
    EXPECT_TRUE(ctx, decision.shouldConsumeCommitted);
    EXPECT_EQ(ctx, static_cast<int>(decision.mode),
              static_cast<int>(FrameReuseModel::ReuseMode::Lockstep));
}

void TestSimulationPreviousFrameConsumesOnlyInAsyncMode(TestContext& ctx)
{
    FrameReuseModel::SimulationReuseInputs inputs{};
    inputs.requestFrameId = 43u;
    inputs.committedFrameId = 42u;
    inputs.writeIdx = 1u;
    inputs.committedIdx = 0u;
    inputs.slaveSimulationEnabled = true;
    inputs.lockstepEnabled = false;
    inputs.jobInFlight = true;
    inputs.hasCommittedPacket = true;

    const auto decision = FrameReuseModel::ComputeSimulationReuseDecision(inputs);
    EXPECT_TRUE(ctx, decision.valid);
    EXPECT_TRUE(ctx, decision.hasPreviousFrameCandidate);
    EXPECT_TRUE(ctx, decision.shouldConsumeCommitted);
    EXPECT_EQ(ctx, static_cast<int>(decision.mode),
              static_cast<int>(FrameReuseModel::ReuseMode::PreviousFrame));
}

void TestSimulationFallsBackWhenNoCommittedAndNoDispatch(TestContext& ctx)
{
    FrameReuseModel::SimulationReuseInputs inputs{};
    inputs.requestFrameId = 10u;
    inputs.writeIdx = 1u;
    inputs.committedIdx = 0u;
    inputs.slaveSimulationEnabled = true;
    inputs.lockstepEnabled = false;
    inputs.jobInFlight = true;
    inputs.hasCommittedPacket = false;

    const auto decision = FrameReuseModel::ComputeSimulationReuseDecision(inputs);
    EXPECT_TRUE(ctx, decision.requiresSynchronousFallback);
    EXPECT_TRUE(ctx, !decision.shouldDispatchNextFrame);
}

void TestTrackLockstepWaitsWithoutExactPacket(TestContext& ctx)
{
    FrameReuseModel::TrackReuseInputs inputs{};
    inputs.requestFrameId = 50u;
    inputs.committedFrameId = 49u;
    inputs.writeIdx = 1u;
    inputs.committedIdx = 0u;
    inputs.renderEnabled = true;
    inputs.lockstepEnabled = true;
    inputs.producerJobInFlight = true;
    inputs.hasCommittedPacket = true;

    const auto decision = FrameReuseModel::ComputeTrackReuseDecision(inputs);
    EXPECT_TRUE(ctx, decision.requiresLockstepWait);
    EXPECT_TRUE(ctx, !decision.shouldConsumeCommitted);
}

void TestTrackAsyncConsumesPreviousFrame(TestContext& ctx)
{
    FrameReuseModel::TrackReuseInputs inputs{};
    inputs.requestFrameId = 50u;
    inputs.committedFrameId = 49u;
    inputs.writeIdx = 1u;
    inputs.committedIdx = 0u;
    inputs.renderEnabled = true;
    inputs.lockstepEnabled = false;
    inputs.producerJobInFlight = true;
    inputs.hasCommittedPacket = true;

    const auto decision = FrameReuseModel::ComputeTrackReuseDecision(inputs);
    EXPECT_TRUE(ctx, decision.shouldConsumeCommitted);
    EXPECT_TRUE(ctx, decision.hasPreviousFrameCandidate);
}
} // namespace

int main()
{
    TestContext ctx{};

    try
    {
        TestSimulationLockstepConsumesExactFrame(ctx);
        TestSimulationPreviousFrameConsumesOnlyInAsyncMode(ctx);
        TestSimulationFallsBackWhenNoCommittedAndNoDispatch(ctx);
        TestTrackLockstepWaitsWithoutExactPacket(ctx);
        TestTrackAsyncConsumesPreviousFrame(ctx);
    }
    catch (const std::exception& ex)
    {
        ctx.Fail("unexpected exception", __FILE__, __LINE__, ex.what());
    }

    if (ctx.failures != 0)
    {
        std::cerr << "frame_reuse_decision_tests: " << ctx.failures << " failure(s)\n";
        return 1;
    }

    std::cout << "frame_reuse_decision_tests: PASS\n";
    return 0;
}
