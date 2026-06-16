#pragma once

#include <cstdint>

struct RealtimeFpsState
{
    static constexpr uint8_t kVblankValidBit = 1u << 0;
    uint32_t lastVblank = 0u;
    uint16_t sampleVblanks = 0u;
    uint8_t sampleFrames = 0u;
    uint8_t framesOver30Budget = 0u;
    uint8_t framesOver60Budget = 0u;
    uint8_t flags = 0u;

    bool VblankValid() const { return (flags & kVblankValidBit) != 0u; }
    void SetVblankValid(bool enabled)
    {
        if (enabled) flags |= kVblankValidBit;
        else flags &= static_cast<uint8_t>(~kVblankValidBit);
    }
};
