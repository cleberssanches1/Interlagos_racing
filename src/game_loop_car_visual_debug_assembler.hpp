#pragma once

#include "game_loop_car_visual_debug_contracts.hpp"
#include "game_loop_car_visual_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedCarVisualDebugPacket(const CarVisualFramePacket& framePacket,
                                     CarVisualDebugPacket& outPacket)
{
    outPacket.valid = framePacket.Valid();
    outPacket.readyForSubmit = framePacket.ReadyForSubmit();
    outPacket.groundedShadow = (framePacket.renderPacket.runtimeDebug.groundMask != 0u);
    outPacket.drawShadowBlob = framePacket.shadowPacket.drawBlob;
    outPacket.drawShadowModel = framePacket.shadowPacket.drawModel;
    outPacket.submitted = framePacket.telemetry.submitted;
    outPacket.shouldReducePressure = framePacket.budgetPolicy.shouldReducePressure;
    outPacket.shouldAvoidOptionalAllocations =
        framePacket.budgetPolicy.shouldAvoidOptionalAllocations;
    outPacket.syncYawDeg = framePacket.syncYawDeg;
    outPacket.renderYawDeg = framePacket.renderPacket.renderYawDeg;
    outPacket.shadowYawDeg = framePacket.shadowPacket.shadowYawDeg;
    outPacket.renderedFaceCount = framePacket.telemetry.renderedFaceCount;
}

inline CarVisualDebugPacket BuildCarVisualDebugPacket(const CarVisualFramePacket& framePacket)
{
    CarVisualDebugPacket packet{};
    SeedCarVisualDebugPacket(framePacket, packet);
    return packet;
}

} // namespace GameLoopRuntime
