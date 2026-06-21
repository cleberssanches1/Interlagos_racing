#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct CarVisualDebugPacket
{
    bool valid = false;
    bool readyForSubmit = false;
    bool groundedShadow = false;
    bool drawShadowBlob = false;
    bool drawShadowModel = false;
    bool submitted = false;
    bool shouldReducePressure = false;
    bool shouldAvoidOptionalAllocations = false;
    int32_t syncYawDeg = 0;
    int32_t renderYawDeg = 0;
    int32_t shadowYawDeg = 0;
    uint16_t renderedFaceCount = 0u;
};

} // namespace GameLoopRuntime
