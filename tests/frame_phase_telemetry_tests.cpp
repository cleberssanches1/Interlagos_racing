#include <cstdint>
#include <iostream>

#include "frame_phase_telemetry.hpp"

namespace
{
int gFailures = 0;

#define EXPECT_TRUE(expr) \
    do { if (!(expr)) { std::cerr << __FILE__ << ":" << __LINE__ << " failure: " #expr "\n"; ++gFailures; } } while (0)

#define EXPECT_EQ(actual, expected) \
    do { const auto a = (actual); const auto e = (expected); if (!(a == e)) { \
        std::cerr << __FILE__ << ":" << __LINE__ << " failure: " #actual " == " #expected \
                  << " actual=" << static_cast<unsigned long long>(a) \
                  << " expected=" << static_cast<unsigned long long>(e) << "\n"; ++gFailures; } } while (0)

void TestThresholds()
{
    using namespace FramePhaseTelemetry;
    EXPECT_EQ(LongStallThresholdVblanks(60u), 6u);
    EXPECT_EQ(LongStallThresholdVblanks(50u), 5u);
    EXPECT_EQ(LongStallThresholdVblanks(0u), 6u);
    EXPECT_EQ(FrtWrapRiskVblanks(60u), 18u);
    EXPECT_EQ(FrtWrapRiskVblanks(50u), 15u);
    EXPECT_EQ(FrtWrapRiskVblanks(0u), 18u);
}

void TestShortSpanAndFrtWrap()
{
    using namespace FramePhaseTelemetry;
    State state{};
    BeginFrame(state, 10u);
    RecordSpan(state,
               Phase::Camera,
               Stamp{100u, 65000u},
               Stamp{102u, 1000u},
               6u);
    EXPECT_EQ(PeakFor(state, Phase::Camera).frtTicks, 1536u);
    EXPECT_EQ(PeakFor(state, Phase::Camera).vblanks, 2u);
    EXPECT_TRUE(!state.worst.valid);
    EXPECT_EQ(state.eventCount, 0u);
}

void TestLongStallLatchesPhase()
{
    using namespace FramePhaseTelemetry;
    State state{};
    BeginFrame(state, 77u);
    RecordSpan(state,
               Phase::Synchronize,
               Stamp{200u, 100u},
               Stamp{238u, 400u},
               6u);
    EXPECT_TRUE(state.worst.valid);
    EXPECT_EQ(static_cast<uint8_t>(state.worst.phase), static_cast<uint8_t>(Phase::Synchronize));
    EXPECT_EQ(state.worst.frameId, 77u);
    EXPECT_EQ(state.worst.vblanks, 38u);
    EXPECT_EQ(state.eventCount, 1u);
    EXPECT_TRUE(state.last.valid);
    EXPECT_EQ(state.last.frameId, 77u);
    EXPECT_EQ(state.last.vblanks, 38u);
}

void TestWorstStallAndNoFrameDoubleCount()
{
    using namespace FramePhaseTelemetry;
    State state{};
    BeginFrame(state, 90u);
    RecordSpan(state, Phase::TrackEnd, Stamp{10u, 0u}, Stamp{18u, 10u}, 6u);
    RecordSpan(state, Phase::Frame, Stamp{5u, 0u}, Stamp{20u, 20u}, 6u);
    EXPECT_EQ(state.eventCount, 1u);
    EXPECT_EQ(state.worst.vblanks, 8u);

    BeginFrame(state, 91u);
    RecordSpan(state, Phase::TrackRender, Stamp{30u, 0u}, Stamp{42u, 30u}, 6u);
    EXPECT_EQ(state.eventCount, 2u);
    EXPECT_EQ(state.worst.frameId, 91u);
    EXPECT_EQ(state.worst.vblanks, 12u);
    EXPECT_EQ(static_cast<uint8_t>(state.worst.phase), static_cast<uint8_t>(Phase::TrackRender));
    EXPECT_EQ(state.last.frameId, 91u);
    EXPECT_EQ(state.last.vblanks, 12u);
}

void TestUnattributedLongFrame()
{
    using namespace FramePhaseTelemetry;
    State state{};
    BeginFrame(state, 123u);
    RecordSpan(state, Phase::Frame, Stamp{100u, 0u}, Stamp{107u, 1u}, 6u);
    EXPECT_EQ(state.eventCount, 1u);
    EXPECT_EQ(static_cast<uint8_t>(state.worst.phase), static_cast<uint8_t>(Phase::Frame));
}

void TestWindowResetPreservesStall()
{
    using namespace FramePhaseTelemetry;
    State state{};
    BeginFrame(state, 42u);
    RecordSpan(state, Phase::Hud, Stamp{0u, 0u}, Stamp{7u, 99u}, 6u);
    ResetWindowPeaks(state);
    EXPECT_EQ(PeakFor(state, Phase::Hud).vblanks, 0u);
    EXPECT_TRUE(state.worst.valid);
    EXPECT_TRUE(state.last.valid);
    EXPECT_EQ(state.eventCount, 1u);
}

void TestUnaccountedTicks()
{
    using namespace FramePhaseTelemetry;
    EXPECT_EQ(ComputeUnaccountedTicks(1000u, 700u, 3u, 18u), 300u);
    EXPECT_EQ(ComputeUnaccountedTicks(1000u, 1200u, 3u, 18u), 0u);
    EXPECT_EQ(ComputeUnaccountedTicks(1000u, 700u, 7u, 18u), 300u);
    EXPECT_EQ(ComputeUnaccountedTicks(1000u, 700u, 18u, 18u), 65535u);
}
}

int main()
{
    TestThresholds();
    TestShortSpanAndFrtWrap();
    TestLongStallLatchesPhase();
    TestWorstStallAndNoFrameDoubleCount();
    TestUnattributedLongFrame();
    TestWindowResetPreservesStall();
    TestUnaccountedTicks();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " failure(s)\n";
        return 1;
    }
    std::cout << "frame phase telemetry tests passed\n";
    return 0;
}
