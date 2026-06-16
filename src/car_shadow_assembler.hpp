#pragma once

#include "car_render_contracts.hpp"

// Assembler passivo de sombra do carro.
// Mantém a decisão blob/model fora do runtime atual até a futura integração.

namespace CarRenderDomain
{

inline void SeedShadowPacket(const CarRenderPacket& renderPacket,
                             CarShadowPacket& outShadowPacket)
{
    outShadowPacket.shadowPosition = renderPacket.renderPosition;
    outShadowPacket.shadowYawDeg = renderPacket.renderYawDeg;
}

inline void AnchorShadowToGround(const CarRenderPacket& renderPacket,
                                 int32_t groundBiasUnits,
                                 CarShadowPacket& ioShadowPacket)
{
    if (renderPacket.runtimeDebug.groundMask != 0u)
    {
        ioShadowPacket.shadowPosition.Y = SRL::Math::Types::Fxp::BuildRaw(
            static_cast<int32_t>(renderPacket.runtimeDebug.groundTargetY) << 16);
    }
    ioShadowPacket.shadowPosition.Y +=
        SRL::Math::Types::Fxp::BuildRaw(groundBiasUnits << 16);
}

inline void ConfigureShadowModes(bool drawBlob,
                                 bool drawModel,
                                 CarShadowPacket& ioShadowPacket)
{
    ioShadowPacket.drawBlob = drawBlob;
    ioShadowPacket.drawModel = drawModel;
}

} // namespace CarRenderDomain
