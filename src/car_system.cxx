#include "car_system.hpp"
#include "exception_stubs.hpp"
#include "render_pipeline.hpp"

#include <cstdlib>

namespace Game
{
CarSystem::CarSystem(ModelObject* carObj, bool smooth, const Config& config)
    : config_(config)
{
    std::snprintf(name_, sizeof(name_), "Car");
    if (!carObj) return;

    MeshRenderer::Config rendererConfig;
    rendererConfig.modelCenter = config_.modelCenter;
    rendererConfig.lightDirection = config_.lightDirection;
    const size_t orderCount = std::min(config_.orderCount, rendererConfig.drawOrder.size());
    rendererConfig.drawOrderCount = orderCount;
    for (size_t i = 0; i < orderCount; ++i)
    {
        rendererConfig.drawOrder[i] = config_.drawOrder[i];
    }
    rendererConfig.wireframeOnly = config_.wireframeOnly;

    renderer_ = std::make_unique<MeshRenderer>(*carObj, smooth, rendererConfig);
    if (!renderer_) return;
    renderer_->SetSkipMesh(kCrashSkipMesh);
    renderer_->SetScale(SRL::Math::Types::Fxp::Convert(0.5f));
}

void CarSystem::UpdateWheels(bool start, bool stop)
{
    // placeholder for wheel animation
    (void)start;
    (void)stop;
}

void CarSystem::Render(int32_t yawDeg)
{
    yawDeg_ = ((yawDeg % 360) + 360) % 360;
}

void CarSystem::SubmitRender(RenderPipeline& pipeline, bool logStats)
{
    if (!renderer_) return;
    SRL::Math::Types::Angle yaw = SRL::Math::Types::Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(yawDeg_));
    pipeline.Enqueue(*renderer_, worldPosition_, yaw, logStats);
}
} // namespace Game
