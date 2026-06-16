#pragma once

#include "car_render_contracts.hpp"

// Assembler passivo do estado visual do carro.
// Não participa do runtime atual; prepara a futura extração do CarRenderSystem.

namespace CarRenderDomain
{

inline void SeedRenderPacket(const FrameContext& context,
                             CarRenderPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.renderPosition = context.worldPosition;
    outPacket.renderYawDeg = context.gameplayYawDeg + context.visualYawOffsetDeg;
    outPacket.runtimeDebug = context.runtimeDebug;
}

inline void ApplyVisualLift(SRL::Math::Types::Vector3D& ioRenderPosition,
                            int32_t liftUnits)
{
    ioRenderPosition.Y -= SRL::Math::Types::Fxp::BuildRaw(liftUnits << 16);
}

inline void ApplyDepthBias(const SRL::Math::Types::Vector3D& cameraLocation,
                           SRL::Math::Types::Vector3D& ioRenderPosition,
                           int32_t biasUnits)
{
    const int32_t dxRaw = cameraLocation.X.RawValue() - ioRenderPosition.X.RawValue();
    const int32_t dzRaw = cameraLocation.Z.RawValue() - ioRenderPosition.Z.RawValue();
    const int32_t adx = (dxRaw < 0) ? -dxRaw : dxRaw;
    const int32_t adz = (dzRaw < 0) ? -dzRaw : dzRaw;
    const int32_t maxAxis = (adx > adz) ? adx : adz;
    if (maxAxis <= 0) return;

    const int32_t biasRaw = (biasUnits << 16);
    const int32_t offXRaw =
        static_cast<int32_t>((static_cast<int64_t>(dxRaw) * biasRaw) / maxAxis);
    const int32_t offZRaw =
        static_cast<int32_t>((static_cast<int64_t>(dzRaw) * biasRaw) / maxAxis);
    ioRenderPosition.X += SRL::Math::Types::Fxp::BuildRaw(offXRaw);
    ioRenderPosition.Z += SRL::Math::Types::Fxp::BuildRaw(offZRaw);
}

} // namespace CarRenderDomain
