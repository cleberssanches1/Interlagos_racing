#pragma once

#include "car_render_transition_ops.hpp"

namespace Game
{

class CarRenderSystem final
{
public:
    using Ports = CarRenderDomain::Ports;
    using FrameContext = CarRenderDomain::FrameContext;
    using RenderPacket = CarRenderDomain::CarRenderPacket;
    using ShadowPacket = CarRenderDomain::CarShadowPacket;
    using SubmitPacket = CarRenderDomain::CarSubmitPacket;
    using Telemetry = CarRenderDomain::CarRenderTelemetry;

    static FrameContext BuildFrameContext(const SRL::Math::Types::Vector3D& worldPosition,
                                          const SRL::Math::Types::Vector3D& cameraLocation,
                                          const SRL::Math::Types::Vector3D& cameraLookTarget,
                                          int32_t gameplayYawDeg,
                                          int32_t visualYawOffsetDeg,
                                          const Game::CarSystem::RuntimeDebugSnapshot& runtimeDebug)
    {
        FrameContext context{};
        context.worldPosition = worldPosition;
        context.cameraLocation = cameraLocation;
        context.cameraLookTarget = cameraLookTarget;
        context.gameplayYawDeg = gameplayYawDeg;
        context.visualYawOffsetDeg = visualYawOffsetDeg;
        context.runtimeDebug = runtimeDebug;
        return context;
    }

    static RenderPacket BuildRenderPacket(const FrameContext& context,
                                          int32_t visualLiftUnits,
                                          int32_t depthBiasUnits)
    {
        return CarRenderDomain::BuildRenderPacket(context, visualLiftUnits, depthBiasUnits);
    }

    static ShadowPacket BuildShadowPacket(const RenderPacket& renderPacket,
                                          bool drawBlob,
                                          bool drawModel,
                                          int32_t groundBiasUnits)
    {
        return CarRenderDomain::BuildShadowPacket(renderPacket,
                                                  drawBlob,
                                                  drawModel,
                                                  groundBiasUnits);
    }

    static SubmitPacket BuildSubmitPacket(const RenderPacket& renderPacket,
                                          const ShadowPacket& shadowPacket)
    {
        return CarRenderDomain::BuildSubmitPacket(renderPacket, shadowPacket);
    }

    static Telemetry BuildTelemetry(uint16_t renderedFaceCount, bool submitted)
    {
        return CarRenderDomain::BuildRenderTelemetry(renderedFaceCount, submitted);
    }
};

} // namespace Game
