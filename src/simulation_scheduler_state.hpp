#pragma once

#include <cstdint>

#include "interfaces.hpp"

namespace Game
{

struct SimulationPayload
{
    GameplayFrameState frameState{};
};

struct SimulationRuntimeState
{
    static constexpr uint8_t kJobInFlightBit = 1u << 0;
    static constexpr uint8_t kHasCompletedBit = 1u << 1;
    SimulationPayload input[2]{};
    SimulationPayload output[2]{};
    uint32_t slaveDispatchCount = 0;
    uint32_t slaveDispatchSkipsTrackBusy = 0;
    uint32_t slaveDispatchSkipsBackoff = 0;
    uint32_t drainSoftTimeouts = 0;
    uint32_t drainHardWaits = 0;
    uint16_t masterWaitTicksThisFrame = 0;
    uint16_t slaveLastJobTicksThisFrame = 0;
    uint8_t writeIdx = 0;
    uint8_t inFlightIdx = 0;
    uint8_t completedIdx = 0;
    uint8_t slaveBackoffFrames = 0;
    uint8_t flags = 0u;

    bool JobInFlight() const { return (flags & kJobInFlightBit) != 0u; }
    bool HasCompleted() const { return (flags & kHasCompletedBit) != 0u; }
    void SetJobInFlight(bool enabled)
    {
        if (enabled) flags |= kJobInFlightBit;
        else flags &= static_cast<uint8_t>(~kJobInFlightBit);
    }
    void SetHasCompleted(bool enabled)
    {
        if (enabled) flags |= kHasCompletedBit;
        else flags &= static_cast<uint8_t>(~kHasCompletedBit);
    }
    void MarkDispatched(uint8_t slot)
    {
        SetJobInFlight(true);
        inFlightIdx = slot;
        writeIdx ^= 1u;
        ++slaveDispatchCount;
    }
    void MarkCompleted(uint8_t slot)
    {
        SetJobInFlight(false);
        SetHasCompleted(true);
        completedIdx = slot;
    }
    void ClearCompleted()
    {
        SetHasCompleted(false);
    }
};

} // namespace Game
