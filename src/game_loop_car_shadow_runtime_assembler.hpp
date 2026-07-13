#pragma once

#include "car_render_system.hpp"
#include "render_pipeline.hpp"

namespace GameLoopRuntime
{

struct CarShadowRuntimeDecision
{
    bool drawBlob = false;
    bool drawModel = false;
    int32_t blobGroundBiasUnits = 0;
    int32_t modelGroundBiasUnits = 0;
};

inline Game::CarRenderSystem::RenderPacket BuildCarRenderRuntimePacket(
    const SRL::Math::Types::Vector3D& worldPosition,
    const SRL::Math::Types::Vector3D& cameraLocation,
    const SRL::Math::Types::Vector3D& cameraLookTarget,
    int32_t gameplayYawDeg,
    int32_t visualYawOffsetDeg,
    const Game::CarSystem::RuntimeDebugSnapshot& runtimeDebug,
    int32_t visualLiftUnits,
    int32_t depthBiasUnits)
{
    return Game::CarRenderSystem::BuildRenderPacket(
        Game::CarRenderSystem::BuildFrameContext(
            worldPosition,
            cameraLocation,
            cameraLookTarget,
            gameplayYawDeg,
            visualYawOffsetDeg,
            runtimeDebug),
        visualLiftUnits,
        depthBiasUnits);
}

inline CarShadowRuntimeDecision BuildCarShadowRuntimeDecision(
    bool renderCarShadowModel,
    bool hasShadowRenderer)
{
    CarShadowRuntimeDecision decision{};
    constexpr bool kEnableCarShadowRendering = true;
    constexpr bool kUseBlobShadow = false;
    constexpr int32_t kShadowGroundBiasUnitsBlob = 10;
    constexpr int32_t kShadowGroundBiasUnitsModel = 1;

    if constexpr (!kEnableCarShadowRendering)
    {
        return decision;
    }

    decision.drawBlob = kUseBlobShadow;
    decision.drawModel = renderCarShadowModel && hasShadowRenderer;
    decision.blobGroundBiasUnits = kShadowGroundBiasUnitsBlob;
    decision.modelGroundBiasUnits = kShadowGroundBiasUnitsModel;
    return decision;
}

inline Game::CarRenderSystem::ShadowPacket BuildCarShadowPrepPacket(
    const Game::CarRenderSystem::RenderPacket& renderPacket,
    bool drawBlob,
    bool drawModel,
    int32_t groundBiasUnits)
{
    return Game::CarRenderSystem::BuildShadowPacket(
        renderPacket,
        drawBlob,
        drawModel,
        groundBiasUnits);
}

inline bool TryBuildCarShadowPrepPacket(
    const Game::CarRenderSystem::RenderPacket& renderPacket,
    bool enabled,
    bool drawBlob,
    bool drawModel,
    int32_t groundBiasUnits,
    Game::CarRenderSystem::ShadowPacket& outShadowPacket)
{
    if (!enabled)
    {
        return false;
    }

    outShadowPacket =
        BuildCarShadowPrepPacket(renderPacket, drawBlob, drawModel, groundBiasUnits);
    return true;
}

inline void ApplyCarRenderRuntimeSync(
    Game::CarSystem& car,
    const Game::CarRenderSystem::RenderPacket& renderPacket,
    int32_t gameplayYawDeg)
{
    car.SyncRenderState(renderPacket.renderPosition, gameplayYawDeg);
}

inline uint16_t CaptureCarRenderRuntimeFaceCount(Game::CarSystem& car)
{
    if (MeshRenderer* renderer = car.Renderer())
    {
        const uint32_t rawFaceCount = renderer->LastRenderFaceCount();
        return static_cast<uint16_t>((rawFaceCount > 0xFFFFu) ? 0xFFFFu : rawFaceCount);
    }

    return 0u;
}

inline Game::CarRenderSystem::Telemetry BuildCarRenderRuntimeTelemetry(
    uint16_t renderedFaceCount)
{
    return Game::CarRenderSystem::BuildTelemetry(renderedFaceCount, true);
}

inline Game::CarRenderSystem::Telemetry SubmitCarRenderRuntime(
    RenderPipeline& renderPipeline,
    Game::CarSystem& car)
{
    renderPipeline.Reset();
    car.SubmitRender(renderPipeline);
    renderPipeline.Flush();
    return BuildCarRenderRuntimeTelemetry(CaptureCarRenderRuntimeFaceCount(car));
}

} // namespace GameLoopRuntime
