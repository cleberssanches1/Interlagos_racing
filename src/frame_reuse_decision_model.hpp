#pragma once

#include <cstdint>

namespace FrameReuseModel
{

enum class ReuseMode : uint8_t
{
    Lockstep = 0,
    PreviousFrame,
    SynchronousFallback
};

struct SimulationReuseInputs
{
    uint32_t requestFrameId = 0u;
    uint32_t committedFrameId = 0u;
    uint8_t writeIdx = 0u;
    uint8_t committedIdx = 0u;
    bool slaveSimulationEnabled = false;
    bool lockstepEnabled = true;
    bool jobInFlight = false;
    bool hasCommittedPacket = false;
};

struct TrackReuseInputs
{
    uint32_t requestFrameId = 0u;
    uint32_t committedFrameId = 0u;
    int16_t activeSegmentId = -1;
    uint8_t writeIdx = 0u;
    uint8_t committedIdx = 0u;
    bool renderEnabled = false;
    bool lockstepEnabled = true;
    bool producerJobInFlight = false;
    bool hasCommittedPacket = false;
};

struct SimulationReuseDecision
{
    bool valid = false;
    bool hasExactFrameCandidate = false;
    bool hasPreviousFrameCandidate = false;
    bool shouldConsumeCommitted = false;
    bool shouldDispatchNextFrame = false;
    bool requiresLockstepWait = false;
    bool requiresSynchronousFallback = false;
    ReuseMode mode = ReuseMode::Lockstep;
    uint8_t consumeSlot = 0u;
    uint8_t dispatchSlot = 0u;
};

struct TrackReuseDecision
{
    bool valid = false;
    bool hasExactFrameCandidate = false;
    bool hasPreviousFrameCandidate = false;
    bool shouldConsumeCommitted = false;
    bool shouldKickProducer = false;
    bool requiresLockstepWait = false;
    bool requiresSynchronousFallback = false;
    ReuseMode mode = ReuseMode::Lockstep;
    uint8_t consumeSlot = 0u;
    uint8_t dispatchSlot = 0u;
};

inline SimulationReuseDecision ComputeSimulationReuseDecision(const SimulationReuseInputs& inputs)
{
    SimulationReuseDecision decision{};
    decision.valid = inputs.slaveSimulationEnabled;
    decision.mode = inputs.lockstepEnabled ? ReuseMode::Lockstep : ReuseMode::PreviousFrame;
    decision.dispatchSlot = inputs.writeIdx;
    decision.consumeSlot = inputs.committedIdx;

    if (!inputs.hasCommittedPacket)
    {
        decision.shouldDispatchNextFrame = inputs.slaveSimulationEnabled && !inputs.jobInFlight;
        decision.requiresLockstepWait = inputs.lockstepEnabled && inputs.jobInFlight;
        decision.requiresSynchronousFallback =
            !inputs.lockstepEnabled && !decision.shouldDispatchNextFrame;
        return decision;
    }

    decision.hasExactFrameCandidate = (inputs.committedFrameId == inputs.requestFrameId);
    decision.hasPreviousFrameCandidate = (inputs.committedFrameId < inputs.requestFrameId);
    decision.shouldConsumeCommitted =
        inputs.lockstepEnabled ? decision.hasExactFrameCandidate
                               : decision.hasPreviousFrameCandidate;
    decision.shouldDispatchNextFrame =
        inputs.slaveSimulationEnabled && !inputs.jobInFlight;
    decision.requiresLockstepWait =
        inputs.lockstepEnabled && inputs.jobInFlight && !decision.hasExactFrameCandidate;
    decision.requiresSynchronousFallback =
        !inputs.lockstepEnabled &&
        !decision.shouldConsumeCommitted &&
        !decision.shouldDispatchNextFrame;
    return decision;
}

inline TrackReuseDecision ComputeTrackReuseDecision(const TrackReuseInputs& inputs)
{
    TrackReuseDecision decision{};
    decision.valid = inputs.renderEnabled;
    decision.mode = inputs.lockstepEnabled ? ReuseMode::Lockstep : ReuseMode::PreviousFrame;
    decision.dispatchSlot = inputs.writeIdx;
    decision.consumeSlot = inputs.committedIdx;

    if (!inputs.renderEnabled)
    {
        return decision;
    }

    if (!inputs.hasCommittedPacket)
    {
        decision.shouldKickProducer = !inputs.producerJobInFlight;
        decision.requiresLockstepWait =
            inputs.lockstepEnabled && inputs.producerJobInFlight;
        decision.requiresSynchronousFallback =
            !inputs.lockstepEnabled && inputs.producerJobInFlight;
        return decision;
    }

    decision.hasExactFrameCandidate = (inputs.committedFrameId == inputs.requestFrameId);
    decision.hasPreviousFrameCandidate = (inputs.committedFrameId < inputs.requestFrameId);
    decision.shouldConsumeCommitted =
        inputs.lockstepEnabled ? decision.hasExactFrameCandidate
                               : decision.hasPreviousFrameCandidate;
    decision.shouldKickProducer = !inputs.producerJobInFlight;
    decision.requiresLockstepWait =
        inputs.lockstepEnabled &&
        inputs.producerJobInFlight &&
        !decision.hasExactFrameCandidate;
    decision.requiresSynchronousFallback =
        !inputs.lockstepEnabled &&
        !decision.shouldConsumeCommitted &&
        inputs.producerJobInFlight;
    return decision;
}

} // namespace FrameReuseModel
