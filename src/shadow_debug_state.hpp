#pragma once

#include <cstdint>

#include <srl.hpp>

struct ShadowDebugState
{
    SRL::Math::Types::Vector3D worldPos{0.0, 0.0, 0.0};
    int32_t yawDeg = 0;
};
