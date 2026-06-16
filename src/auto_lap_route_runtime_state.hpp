#pragma once

#include <array>
#include <cstdint>

#include "track_system.hpp"

struct AutoLapRouteState
{
    static constexpr uint8_t kInitializedBit = 1u << 0;
    static constexpr uint8_t kBuiltBit = 1u << 1;
    static constexpr uint8_t kStartupYawAlignedBit = 1u << 2;

    uint16_t index = 0;
    TrackLowWorkVector<int16_t> ids{};
    TrackLowWorkVector<SRL::Math::Types::Vector3D> centers{};
    TrackLowWorkVector<int16_t> yawDeg{};
    TrackLowWorkVector<int16_t> offDeg{};
    int16_t baseYawDeg = 0;
    int16_t currentOffDeg = 0;
    int8_t selectedGuideLine = -1;
    uint8_t flags = 0u;
    std::array<TrackLowWorkVector<SRL::Math::Types::Vector3D>, 3> guideLines{};

    bool Initialized() const { return (flags & kInitializedBit) != 0u; }
    bool Built() const { return (flags & kBuiltBit) != 0u; }
    bool StartupYawAligned() const { return (flags & kStartupYawAlignedBit) != 0u; }
    void SetInitialized(bool enabled)
    {
        if (enabled) flags |= kInitializedBit;
        else flags &= static_cast<uint8_t>(~kInitializedBit);
    }
    void SetBuilt(bool enabled)
    {
        if (enabled) flags |= kBuiltBit;
        else flags &= static_cast<uint8_t>(~kBuiltBit);
    }
    void SetStartupYawAligned(bool enabled)
    {
        if (enabled) flags |= kStartupYawAlignedBit;
        else flags &= static_cast<uint8_t>(~kStartupYawAlignedBit);
    }
};
