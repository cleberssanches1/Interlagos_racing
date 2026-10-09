#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace FramePhaseTelemetry
{

enum class Phase : uint8_t
{
    Frame = 0,
    Input,
    Gameplay,
    Background,
    Camera,
    TrackRender,
    TrackEnd,
    Car,
    Hud,
    Synchronize,
    Overlay,
    Count
};

constexpr size_t kPhaseCount = static_cast<size_t>(Phase::Count);

struct Stamp
{
    uint32_t vblank = 0u;
    uint16_t frt = 0u;
};

struct PhasePeak
{
    uint16_t frtTicks = 0u;
    uint16_t vblanks = 0u;
};

struct StallEvent
{
    uint32_t frameId = 0u;
    uint16_t vblanks = 0u;
    uint16_t frtTicksModulo = 0u;
    Phase phase = Phase::Frame;
    bool valid = false;
};

struct State
{
    std::array<PhasePeak, kPhaseCount> windowPeaks{};
    StallEvent worst{};
    StallEvent last{};
    uint32_t activeFrameId = 0u;
    uint16_t eventCount = 0u;
    bool longPhaseRecordedThisFrame = false;
};

static_assert(sizeof(State) <= 128u,
              "Frame phase telemetry must remain a small fixed-size runtime state");

inline uint16_t ClampU16(uint32_t value)
{
    return static_cast<uint16_t>(
        value > static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())
            ? std::numeric_limits<uint16_t>::max()
            : value);
}

inline uint16_t LongStallThresholdVblanks(uint32_t refreshHz)
{
    // Roughly 100 ms: 6 VBlanks at 60 Hz, 5 at 50 Hz.
    if (refreshHz == 0u) return 6u;
    return static_cast<uint16_t>((refreshHz + 9u) / 10u);
}

constexpr uint16_t FrtWrapRiskVblanks(uint32_t refreshHz)
{
    // TIM_CKS_128 at 28.636 MHz wraps its 16-bit counter after roughly
    // 292 ms. Keep exact modulo subtraction for shorter long frames.
    if (refreshHz == 0u) refreshHz = 60u;
    return static_cast<uint16_t>((refreshHz * 293u + 999u) / 1000u);
}

inline uint16_t ElapsedFrtTicks(const Stamp& start, const Stamp& end)
{
    // The modulo delta remains useful for short phases. Long phases are
    // classified by the independent 32-bit VBlank counter.
    return static_cast<uint16_t>(end.frt - start.frt);
}

inline uint16_t ElapsedVblanks(const Stamp& start, const Stamp& end)
{
    return ClampU16(end.vblank - start.vblank);
}

inline const char* PhaseCode(Phase phase)
{
    switch (phase)
    {
        case Phase::Frame: return "FR";
        case Phase::Input: return "IN";
        case Phase::Gameplay: return "GP";
        case Phase::Background: return "BG";
        case Phase::Camera: return "CA";
        case Phase::TrackRender: return "TR";
        case Phase::TrackEnd: return "TE";
        case Phase::Car: return "CR";
        case Phase::Hud: return "HD";
        case Phase::Synchronize: return "SY";
        case Phase::Overlay: return "OV";
        default: return "--";
    }
}

inline void BeginFrame(State& state, uint32_t frameId)
{
    state.activeFrameId = frameId;
    state.longPhaseRecordedThisFrame = false;
}

inline void RecordStall(State& state,
                        Phase phase,
                        uint16_t vblanks,
                        uint16_t frtTicks)
{
    if (state.eventCount < std::numeric_limits<uint16_t>::max())
    {
        ++state.eventCount;
    }
    state.last.frameId = state.activeFrameId;
    state.last.vblanks = vblanks;
    state.last.frtTicksModulo = frtTicks;
    state.last.phase = phase;
    state.last.valid = true;
    if (!state.worst.valid || vblanks > state.worst.vblanks)
    {
        state.worst.frameId = state.activeFrameId;
        state.worst.vblanks = vblanks;
        state.worst.frtTicksModulo = frtTicks;
        state.worst.phase = phase;
        state.worst.valid = true;
    }
}

inline void RecordSpan(State& state,
                       Phase phase,
                       const Stamp& start,
                       const Stamp& end,
                       uint16_t longStallThresholdVblanks)
{
    const size_t phaseIndex = static_cast<size_t>(phase);
    if (phaseIndex >= state.windowPeaks.size()) return;

    const uint16_t frtTicks = ElapsedFrtTicks(start, end);
    const uint16_t vblanks = ElapsedVblanks(start, end);
    PhasePeak& peak = state.windowPeaks[phaseIndex];
    if (frtTicks > peak.frtTicks) peak.frtTicks = frtTicks;
    if (vblanks > peak.vblanks) peak.vblanks = vblanks;

    if (vblanks < longStallThresholdVblanks) return;
    if (phase == Phase::Frame && state.longPhaseRecordedThisFrame) return;

    RecordStall(state, phase, vblanks, frtTicks);
    if (phase != Phase::Frame)
    {
        state.longPhaseRecordedThisFrame = true;
    }
}

inline const PhasePeak& PeakFor(const State& state, Phase phase)
{
    return state.windowPeaks[static_cast<size_t>(phase)];
}

inline void ResetWindowPeaks(State& state)
{
    state.windowPeaks = {};
}

inline uint16_t ComputeUnaccountedTicks(uint16_t frameTicks,
                                        uint32_t knownTicks,
                                        uint16_t frameVblanks,
                                        uint16_t frtWrapRiskVblanks)
{
    // Saturate only when the 16-bit FRT may actually have wrapped. Frames in
    // the 100-117 ms range remain exactly measurable and are the useful case.
    if (frameVblanks >= frtWrapRiskVblanks)
    {
        return std::numeric_limits<uint16_t>::max();
    }
    if (knownTicks >= static_cast<uint32_t>(frameTicks)) return 0u;
    return static_cast<uint16_t>(static_cast<uint32_t>(frameTicks) - knownTicks);
}

} // namespace FramePhaseTelemetry
