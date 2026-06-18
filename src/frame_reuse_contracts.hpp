#pragma once

#include <cstdint>

#include "frame_pipeline_contracts.hpp"

namespace FrameReuseDomain
{

enum class ReuseMode : uint8_t
{
    Lockstep = 0,
    PreviousFrame,
    SynchronousFallback
};

struct SimulationFrameHistoryState
{
    Game::SimulationFramePacket packets[2]{};
    uint8_t writeIdx = 0u;
    uint8_t committedIdx = 0u;
    bool hasCommittedPacket = false;
};

struct TrackFrameHistoryState
{
    Game::TrackRenderPacket packets[2]{};
    uint8_t writeIdx = 0u;
    uint8_t committedIdx = 0u;
    bool hasCommittedPacket = false;
};

struct SimulationReuseDecisionPacket
{
    bool valid = false;
    bool hasCommittedPacket = false;
    bool hasExactFrameCandidate = false;
    bool hasPreviousFrameCandidate = false;
    bool shouldConsumeCommitted = false;
    bool shouldDispatchNextFrame = false;
    bool requiresLockstepWait = false;
    bool requiresSynchronousFallback = false;
    ReuseMode mode = ReuseMode::Lockstep;
    uint8_t consumeSlot = 0u;
    uint8_t dispatchSlot = 0u;
    uint32_t committedFrameId = 0u;
    uint32_t requestFrameId = 0u;
};

struct TrackReuseDecisionPacket
{
    bool valid = false;
    bool renderEnabled = false;
    bool hasCommittedPacket = false;
    bool hasExactFrameCandidate = false;
    bool hasPreviousFrameCandidate = false;
    bool shouldConsumeCommitted = false;
    bool shouldKickProducer = false;
    bool requiresLockstepWait = false;
    bool requiresSynchronousFallback = false;
    ReuseMode mode = ReuseMode::Lockstep;
    uint8_t consumeSlot = 0u;
    uint8_t dispatchSlot = 0u;
    uint32_t committedFrameId = 0u;
    uint32_t requestFrameId = 0u;
    int16_t activeSegmentId = -1;
};

struct FrameReuseTelemetry
{
    uint32_t simulationPacketsCommitted = 0u;
    uint32_t simulationPreviousFrameConsumes = 0u;
    uint32_t simulationLockstepConsumes = 0u;
    uint32_t simulationFallbacks = 0u;
    uint32_t trackPacketsCommitted = 0u;
    uint32_t trackPreviousFrameConsumes = 0u;
    uint32_t trackLockstepConsumes = 0u;
    uint32_t trackFallbacks = 0u;
};

} // namespace FrameReuseDomain
