#pragma once

#include <cstdint>

#include <srl.hpp>

struct CameraPathRuntimeState
{
    static constexpr uint8_t kPrevCarWorldPositionValidBit = 1u << 0;
    SRL::Math::Types::Vector3D prevCarWorldPosition{
        SRL::Math::Types::Fxp::BuildRaw(0),
        SRL::Math::Types::Fxp::BuildRaw(0),
        SRL::Math::Types::Fxp::BuildRaw(0)};
    uint8_t flags = 0u;

    bool PrevCarWorldPositionValid() const
    {
        return (flags & kPrevCarWorldPositionValidBit) != 0u;
    }
    void SetPrevCarWorldPositionValid(bool enabled)
    {
        if (enabled) flags |= kPrevCarWorldPositionValidBit;
        else flags &= static_cast<uint8_t>(~kPrevCarWorldPositionValidBit);
    }
};
