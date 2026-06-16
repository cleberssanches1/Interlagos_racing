#pragma once

#include "car_render_state_assembler.hpp"
#include "car_shadow_assembler.hpp"
#include "car_render_submitter.hpp"

// Operacoes passivas externas para futura reintroducao de `CarRenderSystem`.
// Nao devem ser integradas ao runtime critico nesta fase.

namespace CarRenderDomain
{

inline CarRenderPacket BuildRenderPacket(const FrameContext& context,
                                         int32_t visualLiftUnits,
                                         int32_t depthBiasUnits)
{
    CarRenderPacket packet{};
    SeedRenderPacket(context, packet);
    ApplyVisualLift(packet.renderPosition, visualLiftUnits);
    ApplyDepthBias(context.cameraLocation, packet.renderPosition, depthBiasUnits);
    return packet;
}

inline CarShadowPacket BuildShadowPacket(const CarRenderPacket& renderPacket,
                                         bool drawBlob,
                                         bool drawModel,
                                         int32_t groundBiasUnits)
{
    CarShadowPacket packet{};
    SeedShadowPacket(renderPacket, packet);
    AnchorShadowToGround(renderPacket, groundBiasUnits, packet);
    ConfigureShadowModes(drawBlob, drawModel, packet);
    return packet;
}

inline CarSubmitPacket BuildSubmitPacket(const CarRenderPacket& renderPacket,
                                         const CarShadowPacket& shadowPacket)
{
    CarSubmitPacket packet{};
    SeedSubmitPacket(renderPacket, shadowPacket, packet);
    return packet;
}

inline CarRenderTelemetry BuildRenderTelemetry(uint16_t renderedFaceCount,
                                               bool submitted)
{
    CarRenderTelemetry telemetry{};
    if (submitted)
    {
        MarkSubmitIssued(telemetry);
    }
    RecordRenderedFaceCount(renderedFaceCount, telemetry);
    return telemetry;
}

} // namespace CarRenderDomain
