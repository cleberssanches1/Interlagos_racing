#include "car_system.hpp"
#include "exception_stubs.hpp"
#include "render_pipeline.hpp"

#include <algorithm>
#include <cstdlib>

namespace Game
{
void CarSystem::CarCommandAdapter::Accelerate()
{
    if (!state) return;
    state->braking = false;
    int16_t next = static_cast<int16_t>(state->throttle + CarSystem::kThrottleStep);
    state->throttle = std::min<int16_t>(next, CarSystem::kThrottleMax);
}

void CarSystem::CarCommandAdapter::Brake()
{
    if (!state) return;
    state->braking = true;
    int16_t next = static_cast<int16_t>(state->throttle - (CarSystem::kThrottleStep * 2));
    state->throttle = std::max<int16_t>(next, 0);
}

void CarSystem::CarCommandAdapter::SteerLeft()
{
    if (!state) return;
    int16_t next = static_cast<int16_t>(state->steering - CarSystem::kSteeringStep);
    state->steering = std::max<int16_t>(next, static_cast<int16_t>(-CarSystem::kSteeringMax));
}

void CarSystem::CarCommandAdapter::SteerRight()
{
    if (!state) return;
    int16_t next = static_cast<int16_t>(state->steering + CarSystem::kSteeringStep);
    state->steering = std::min<int16_t>(next, CarSystem::kSteeringMax);
}

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
    // Keep car proportions at original model scale.
    renderer_->SetScale(SRL::Math::Types::Fxp::BuildRaw(1 << 16));
}

void CarSystem::UpdateWheels(bool start, bool stop)
{
    if (start)
    {
        commandState_.wheelsSpinning = true;
    }
    if (stop)
    {
        commandState_.wheelsSpinning = false;
    }
    if (commandState_.wheelsSpinning)
    {
        ++commandState_.wheelSpinTicks;
    }
}

void CarSystem::TickCommandState()
{
    // Smooth steering to center when there is no explicit command.
    if (commandState_.steering > 0)
    {
        commandState_.steering = static_cast<int16_t>(std::max<int16_t>(0, commandState_.steering - CarSystem::kSteeringDecay));
    }
    else if (commandState_.steering < 0)
    {
        commandState_.steering = static_cast<int16_t>(std::min<int16_t>(0, commandState_.steering + CarSystem::kSteeringDecay));
    }

    // Idle throttle decay keeps command state stable when not accelerating.
    if (!commandState_.braking && commandState_.throttle > 0)
    {
        commandState_.throttle = static_cast<int16_t>(std::max<int16_t>(0, commandState_.throttle - CarSystem::kThrottleDecay));
    }

    // Brake flag is transient and decays automatically.
    if (commandState_.braking)
    {
        commandState_.throttle = static_cast<int16_t>(std::max<int16_t>(0, commandState_.throttle - CarSystem::kBrakeReleaseDecay));
        if (commandState_.throttle == 0)
        {
            commandState_.braking = false;
        }
    }
}

void CarSystem::Render(int32_t yawDeg)
{
    yawDeg_ = NormalizeYawDeg(yawDeg);
    TickCommandState();
}

void CarSystem::SubmitRender(RenderPipeline& pipeline, bool logStats)
{
    if (!renderer_) return;
    const int32_t renderYawDeg = CurrentRenderYawDeg();
    SRL::Math::Types::Angle yaw =
        SRL::Math::Types::Angle::FromDegrees(SRL::Math::Types::Fxp::BuildRaw(renderYawDeg << 16));
    pipeline.Enqueue(*renderer_, worldPosition_, yaw, logStats);
}
} // namespace Game
