#pragma once

#include <cstdint>

struct CarPrepareRuntimeState
{
    static constexpr uint8_t kJobInFlightBit = 1u << 0;
    static constexpr uint8_t kHasCompletedBit = 1u << 1;
    int32_t inputYaw[2]{};
    int32_t outputYaw[2]{};
    uint8_t writeIdx = 0;
    uint8_t inFlightIdx = 0;
    uint8_t completedIdx = 0;
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
};
